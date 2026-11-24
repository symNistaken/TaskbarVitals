(() => {
  const html = document.documentElement;
  const themeBtn = document.getElementById("theme-toggle");

  // 1. Site theme toggle (shared preference with index.html)
  const savedTheme = localStorage.getItem("tv-site-theme");
  if (savedTheme === "light" || savedTheme === "dark") {
    html.setAttribute("data-theme", savedTheme);
  }
  themeBtn.addEventListener("click", () => {
    const next = html.getAttribute("data-theme") === "dark" ? "light" : "dark";
    html.setAttribute("data-theme", next);
    localStorage.setItem("tv-site-theme", next);
  });

  document.querySelectorAll(".mobile-nav-panel a").forEach((link) => {
    link.addEventListener("click", () => {
      const details = link.closest("details");
      if (details) details.open = false;
    });
  });

  const MIXED_CATEGORY_MAP = {
    "Akko": ["mice", "keyboards"],
    "ASUS ROG": ["mice", "keyboards", "audio"],
    "ATK / VXE / VGN": ["mice", "keyboards", "audio"],
    "Attack Shark & Shared Receivers": ["mice", "keyboards"],
    "AULA": ["mice", "keyboards", "audio"],
    "CHERRY / XTRFY": ["mice", "keyboards"],
    "Corsair": ["mice", "keyboards", "audio"],
    "DAREU": ["mice", "keyboards"],
    "HyperX": ["mice", "audio"],
    "Keychron": ["mice", "keyboards"],
    "Logitech": ["mice", "keyboards", "audio"],
    "MCHOSE": ["mice", "keyboards", "audio"],
    "Motospeed / Delux / Ajazz": ["mice", "keyboards"],
    "Razer": ["mice", "keyboards", "audio"],
    "Redragon": ["mice", "keyboards"],
    "Sony": ["audio", "controllers"],
    "SteelSeries": ["mice", "audio"],
  };

  const catLabels = {
    mice: "Mice",
    keyboards: "Keyboards",
    audio: "Audio",
    controllers: "Controllers & Phones",
    mixed: "Mixed",
    fallbacks: "Fallbacks",
  };

  const searchInput = document.getElementById("compat-search");
  const clearBtn = document.getElementById("compat-clear");
  const testedToggle = document.getElementById("compat-tested-only");
  const tbody = document.getElementById("compat-tbody");
  const tableWrap = tbody.closest(".hw-table-wrap");
  const countEl = document.getElementById("compat-count");
  const emptyBox = document.getElementById("compat-empty");
  const emptyMsg = document.getElementById("compat-empty-msg");
  const resetBtn = document.getElementById("compat-reset-btn");
  const filterBtns = Array.from(document.querySelectorAll(".compat-filters .filter-btn"));

  let devices = [];
  let activeCat = "all";
  let loadFailed = false;

  // 2. Restore query/filter state from URL
  function applyUrlState() {
    const params = new URLSearchParams(window.location.search);
    const initialQuery = params.get("q") || "";
    const initialCat = params.get("cat") || "all";
    const initialTested = params.get("tested") === "1";

    searchInput.value = initialQuery;
    activeCat = Object.prototype.hasOwnProperty.call(catLabels, initialCat) ? initialCat : "all";
    testedToggle.checked = initialTested;
  }

  applyUrlState();

  function updateFilterButtons() {
    filterBtns.forEach((btn) => {
      const active = btn.dataset.cat === activeCat;
      btn.classList.toggle("is-active", active);
      btn.setAttribute("aria-pressed", String(active));
    });
  }

  function syncUrl(pushHistory = false) {
    const next = new URLSearchParams();
    const q = searchInput.value.trim();
    if (q) next.set("q", q);
    if (activeCat !== "all") next.set("cat", activeCat);
    if (testedToggle.checked) next.set("tested", "1");
    const qs = next.toString();
    const target = qs ? `${window.location.pathname}?${qs}` : window.location.pathname;
    if (target === window.location.pathname + window.location.search) return;
    if (pushHistory) {
      window.history.pushState(null, "", target);
    } else {
      window.history.replaceState(null, "", target);
    }
  }

  function matchesCategory(entry) {
    if (activeCat === "all") return true;
    if (entry.category === activeCat) return true;
    const mapped = MIXED_CATEGORY_MAP[entry.vendor];
    if (Array.isArray(mapped) && mapped.includes(activeCat)) return true;
    if (entry.category !== "mixed") return false;
    const text = `${entry.models || ""} ${entry.protocol || ""}`.toLowerCase();
    if (activeCat === "mice") return /\bmice\b|\bmouse\b/.test(text);
    if (activeCat === "keyboards") return /\bkeyboards?\b/.test(text);
    if (activeCat === "audio") return /\bheadsets?\b|\bearbuds?\b|\bheadphones?\b|\bspeaker\b/.test(text);
    if (activeCat === "controllers") return /\bcontrollers?\b|\bgamepad\b|\bdualsense\b|\bdualshock\b/.test(text);
    return false;
  }

  function matchesEntry(entry, rawQuery) {
    if (!matchesCategory(entry)) return false;
    if (testedToggle.checked && !entry.tested) return false;
    if (!rawQuery) return true;

    const terms = rawQuery.toLowerCase().split(/\s+/).filter(Boolean);
    if (terms.length === 0) return true;

    const vidList = Array.isArray(entry.vidPids) ? entry.vidPids : [];
    const catLabel = catLabels[entry.category] || entry.category || "";
    const haystack = [
      entry.vendor || "",
      catLabel,
      entry.models || "",
      entry.protocol || "",
      vidList.join(" "),
    ]
      .join("\n")
      .toLowerCase();
    const haystackNoColon = haystack.replace(/:/g, "");

    return terms.every((term) => {
      if (haystack.includes(term)) return true;
      const noColon = term.replace(/:/g, "");
      return noColon.length >= 4 && haystackNoColon.includes(noColon);
    });
  }

  function appendMultilineText(container, text) {
    const lines = String(text || "").split("\n");
    lines.forEach((line, idx) => {
      if (idx > 0) container.appendChild(document.createElement("br"));
      container.appendChild(document.createTextNode(line));
    });
  }

  function render(pushHistory = false, skipSync = false) {
    const rawQuery = searchInput.value.trim();
    clearBtn.hidden = rawQuery.length === 0;
    updateFilterButtons();
    if (!skipSync) syncUrl(pushHistory);

    if (loadFailed) {
      tableWrap.hidden = true;
      emptyBox.hidden = false;
      resetBtn.hidden = true;
      countEl.textContent = "Unable to load devices.json";
      emptyMsg.textContent =
        "Could not fetch ./devices.json. When previewing locally, stage _site/ (copying docs/ into _site/docs per .github/workflows/pages.yml) and serve _site/ over HTTP.";
      return;
    }

    const filtered = devices.filter((entry) => matchesEntry(entry, rawQuery));
    const verifiedTotal = devices.filter((entry) => entry.tested).length;
    tbody.innerHTML = "";

    if (filtered.length === 0) {
      tableWrap.hidden = true;
      emptyBox.hidden = false;
      resetBtn.hidden = false;
      emptyMsg.textContent =
        "No matching devices, protocols, or VID:PIDs found. Clear filters or submit TaskbarVitals.exe --dump output on GitHub.";
    } else {
      tableWrap.hidden = false;
      emptyBox.hidden = true;

      const frag = document.createDocumentFragment();
      filtered.forEach((entry) => {
        const tr = document.createElement("tr");

        const tdVendor = document.createElement("td");
        tdVendor.className = "compat-vendor-cell";

        const vendorTitle = document.createElement("div");
        vendorTitle.className = "hw-brand compat-vendor-name";
        vendorTitle.textContent = entry.vendor;
        tdVendor.appendChild(vendorTitle);

        const badges = document.createElement("div");
        badges.className = "compat-badges";

        const catBadge = document.createElement("span");
        catBadge.className = "badge-cat";
        catBadge.textContent = catLabels[entry.category] || entry.category;
        badges.appendChild(catBadge);

        if (entry.tested) {
          const testedBadge = document.createElement("span");
          testedBadge.className = "badge-tested";
          testedBadge.textContent = "Tested";
          testedBadge.title = "At least one model in this family was physically tested";
          badges.appendChild(testedBadge);
        }
        tdVendor.appendChild(badges);

        const tdModels = document.createElement("td");
        tdModels.className = "compat-models";
        const modelsText = document.createElement("div");
        appendMultilineText(modelsText, entry.models);
        tdModels.appendChild(modelsText);

        if (Array.isArray(entry.vidPids) && entry.vidPids.length > 0) {
          const vpList = document.createElement("div");
          vpList.className = "vidpid-list";
          entry.vidPids.forEach((vp) => {
            const chip = document.createElement("button");
            chip.type = "button";
            chip.className = "vidpid-pill mono";
            chip.textContent = vp;
            chip.title = `Filter by ${vp}`;
            chip.addEventListener("click", () => {
              searchInput.value = vp;
              render(true);
              searchInput.focus();
            });
            vpList.appendChild(chip);
          });
          tdModels.appendChild(vpList);
        }

        const tdProto = document.createElement("td");
        tdProto.className = "compat-proto";
        appendMultilineText(tdProto, entry.protocol);

        tr.append(tdVendor, tdModels, tdProto);
        frag.appendChild(tr);
      });
      tbody.appendChild(frag);
    }

    countEl.textContent = `Showing ${filtered.length} of ${devices.length} entries (${verifiedTotal} hardware-verified)`;
  }

  searchInput.addEventListener("input", () => render(false));
  clearBtn.addEventListener("click", () => {
    searchInput.value = "";
    render(true);
    searchInput.focus();
  });
  testedToggle.addEventListener("change", () => render(true));
  filterBtns.forEach((btn) => {
    btn.addEventListener("click", () => {
      activeCat = btn.dataset.cat || "all";
      render(true);
    });
  });
  resetBtn.addEventListener("click", () => {
    searchInput.value = "";
    activeCat = "all";
    testedToggle.checked = false;
    render(true);
  });

  window.addEventListener("popstate", () => {
    applyUrlState();
    render(false, true);
  });

  fetch("./devices.json")
    .then((res) => {
      if (!res.ok) throw new Error("HTTP " + res.status);
      return res.json();
    })
    .then((data) => {
      if (!Array.isArray(data)) throw new Error("Invalid dataset format");
      devices = data;
      render(false);
    })
    .catch(() => {
      loadFailed = true;
      render(false);
    });
})();
