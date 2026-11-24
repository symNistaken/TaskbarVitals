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

  const catLabels = {
    mice: "Mice",
    keyboards: "Keyboards",
    audio: "Headsets & Earbuds",
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
  const params = new URLSearchParams(window.location.search);
  const initialQuery = params.get("q") || "";
  const initialCat = params.get("cat") || "all";
  const initialTested = params.get("tested") === "1";

  if (initialQuery) searchInput.value = initialQuery;
  if (Object.prototype.hasOwnProperty.call(catLabels, initialCat)) {
    activeCat = initialCat;
  }
  testedToggle.checked = initialTested;

  function updateFilterButtons() {
    filterBtns.forEach((btn) => {
      btn.classList.toggle("is-active", btn.dataset.cat === activeCat);
    });
  }

  function syncUrl() {
    const next = new URLSearchParams();
    const q = searchInput.value.trim();
    if (q) next.set("q", q);
    if (activeCat !== "all") next.set("cat", activeCat);
    if (testedToggle.checked) next.set("tested", "1");
    const qs = next.toString();
    const target = qs ? `${window.location.pathname}?${qs}` : window.location.pathname;
    window.history.replaceState(null, "", target);
  }

  function matchesEntry(entry, rawQuery) {
    if (activeCat !== "all" && entry.category !== activeCat) return false;
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

  function render() {
    const rawQuery = searchInput.value.trim();
    clearBtn.hidden = rawQuery.length === 0;
    updateFilterButtons();
    syncUrl();

    if (loadFailed) {
      tableWrap.hidden = true;
      emptyBox.hidden = false;
      resetBtn.hidden = true;
      countEl.textContent = "Unable to load devices.json";
      emptyMsg.textContent =
        "Could not fetch ./devices.json. If previewing locally, serve site/ over a local HTTP server instead of file://.";
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
              render();
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

  searchInput.addEventListener("input", render);
  clearBtn.addEventListener("click", () => {
    searchInput.value = "";
    render();
    searchInput.focus();
  });
  testedToggle.addEventListener("change", render);
  filterBtns.forEach((btn) => {
    btn.addEventListener("click", () => {
      activeCat = btn.dataset.cat || "all";
      render();
    });
  });
  resetBtn.addEventListener("click", () => {
    searchInput.value = "";
    activeCat = "all";
    testedToggle.checked = false;
    render();
  });

  fetch("./devices.json")
    .then((res) => {
      if (!res.ok) throw new Error("HTTP " + res.status);
      return res.json();
    })
    .then((data) => {
      if (!Array.isArray(data)) throw new Error("Invalid dataset format");
      devices = data;
      render();
    })
    .catch(() => {
      loadFailed = true;
      render();
    });
})();
