(() => {
  const html = document.documentElement;
  const themeBtn = document.getElementById("theme-toggle");

  // 1. Site theme toggle
  const savedTheme = localStorage.getItem("tv-site-theme");
  if (savedTheme === "light" || savedTheme === "dark") {
    html.setAttribute("data-theme", savedTheme);
  }
  themeBtn.addEventListener("click", () => {
    const next = html.getAttribute("data-theme") === "dark" ? "light" : "dark";
    html.setAttribute("data-theme", next);
    localStorage.setItem("tv-site-theme", next);
  });

  // Close mobile nav drawer when a hash link is selected
  document.querySelectorAll(".mobile-nav-panel a").forEach((link) => {
    link.addEventListener("click", () => {
      const details = link.closest("details");
      if (details) details.open = false;
    });
  });

  // 2. Interactive Windows Taskbar + Rich Sparkline Hover Card Simulator
  const stage = document.getElementById("desktop-stage");
  const stageTop = document.getElementById("sim-stage-top");
  const taskbar = document.getElementById("taskbar-preview");
  const stripSys = document.getElementById("strip-system");
  const stripBat = document.getElementById("strip-batteries");
  const cardDot = document.getElementById("card-dot");
  const cardTitle = document.getElementById("card-title");
  const cardMeta = document.getElementById("card-meta");
  const cardPrimary = document.getElementById("card-primary");
  const cardFill = document.getElementById("card-bar-fill");
  const cardSub = document.getElementById("card-sub");
  const cardChartTitle = document.getElementById("card-chart-title");
  const cardSvg = document.getElementById("card-svg");
  const cardAxis = document.getElementById("card-axis");

  const valCpu = document.getElementById("val-cpu");
  const valRam = document.getElementById("val-ram");
  const valGpu = document.getElementById("val-gpu");
  const valVram = document.getElementById("val-vram");
  const valSsd = document.getElementById("val-ssd");
  const valNet = document.getElementById("val-net");
  const valSysBat = document.getElementById("val-sysbat");

  const itemCpu = document.getElementById("item-cpu");
  const itemGpu = document.getElementById("item-gpu");
  const barCpu = document.getElementById("bar-cpu").firstElementChild;
  const barRam = document.getElementById("bar-ram").firstElementChild;
  const barGpu = document.getElementById("bar-gpu").firstElementChild;
  const barVram = document.getElementById("bar-vram").firstElementChild;
  const barSsd = document.getElementById("bar-ssd").firstElementChild;
  const barSysBat = document.getElementById("bar-sysbat").firstElementChild;

  const simState = {
    style: "row",
    layout: "dual",
    preset: "normal",
    inspected: "gpu",
  };

  const presets = {
    normal: {
      cpuPct: 12, cpuC: 65, cpuW: 21, cpuHeat: "",
      ramPct: 60, ramSub: "14.4 / 23.8 GB used",
      gpuPct: 23, gpuC: 63, gpuW: 22, gpuClock: 2655, gpuMemClock: 8001, gpuHeat: "",
      vramPct: 66, vramSub: "3.8 / 5.8 GB dedicated",
      ssdPct: 0, ssdC: 59, ssdSub: "Micron NVMe 512 GB | 217 GB free",
      netText: "↓23K ↑8K", netPrimary: "↓23 KB/s · ↑8 KB/s", netPct: 14, netPts: [6, 10, 8, 14, 12, 9, 11, 16, 14],
      netSub: "Active NIC | Built-in test: ↓94 / ↑28 Mbps",
      batPct: 88, batStrip: "88% -18W", batPrimary: "88% · -18.4 W", batSub: "Discharging | ~3 h 15 min remaining",
    },
    elevated: {
      cpuPct: 64, cpuC: 78, cpuW: 48, cpuHeat: "",
      ramPct: 74, ramSub: "17.6 / 23.8 GB used",
      gpuPct: 89, gpuC: 83, gpuW: 112, gpuClock: 2715, gpuMemClock: 8001, gpuHeat: "heat-warn",
      vramPct: 82, vramSub: "4.8 / 5.8 GB dedicated",
      ssdPct: 14, ssdC: 62, ssdSub: "Micron NVMe 512 GB | R 340 MB/s · W 95 MB/s",
      netText: "↓4.2M ↑310K", netPrimary: "↓4.2 MB/s · ↑310 KB/s", netPct: 58, netPts: [18, 32, 45, 52, 49, 64, 55, 61, 58],
      netSub: "Active NIC | Built-in test: ↓94 / ↑28 Mbps",
      batPct: 74, batStrip: "74% -46W", batPrimary: "74% · -46.2 W", batSub: "Gaming load | ~1 h 18 min remaining",
    },
    high: {
      cpuPct: 96, cpuC: 92, cpuW: 78, cpuHeat: "heat-high",
      ramPct: 86, ramSub: "20.5 / 23.8 GB used",
      gpuPct: 99, gpuC: 88, gpuW: 138, gpuClock: 2760, gpuMemClock: 8001, gpuHeat: "heat-warn",
      vramPct: 94, vramSub: "5.5 / 5.8 GB dedicated",
      ssdPct: 48, ssdC: 67, ssdSub: "Micron NVMe 512 GB | Peak R/W active",
      netText: "↓11.4M ↑1.8M", netPrimary: "↓11.4 MB/s · ↑1.8 MB/s", netPct: 92, netPts: [42, 68, 74, 88, 91, 84, 96, 89, 92],
      netSub: "Active NIC | Built-in test: ↓94 / ↑28 Mbps",
      batPct: 91, batStrip: "91% +45W", batPrimary: "91% · +45.0 W", batSub: "AC connected | Charging to full",
    },
  };

  function buildSparklineSvg(points, color, isDeviceForecast) {
    const w = 260;
    const h = 44;
    const midY = (v) => (h - 6 - ((v / 100) * (h - 12))).toFixed(1);
    const coords = points.map((v, i) => {
      const xMax = isDeviceForecast ? 132 : w - 4;
      const x = 4 + (i / (points.length - 1)) * (xMax - 4);
      return `${x.toFixed(1)},${midY(v)}`;
    });
    const endX = isDeviceForecast ? "132.0" : String(w - 4);
    const areaD = `M 4.0,${h - 2} L ${coords.join(" L ")} L ${endX},${h - 2} Z`;
    const lineD = `M ${coords.join(" L ")}`;

    let extra = "";
    if (isDeviceForecast) {
      const lastPt = points[points.length - 1];
      const ly = midY(lastPt);
      extra =
        `<line x1="132" y1="2" x2="132" y2="${h - 2}" stroke="rgba(148,163,184,0.4)" stroke-dasharray="2 2"/>` +
        `<line x1="132" y1="${ly}" x2="${w - 4}" y2="${h - 4}" stroke="${color}" stroke-width="2" stroke-dasharray="4 4"/>` +
        `<circle cx="132" cy="${ly}" r="3" fill="${color}"/>`;
    } else {
      const lx = w - 4;
      const ly = midY(points[points.length - 1]);
      extra = `<circle cx="${lx}" cy="${ly}" r="3" fill="${color}"/>`;
    }

    return (
      `<path d="${areaD}" fill="${color}" fill-opacity="0.14"/>` +
      `<path d="${lineD}" fill="none" stroke="${color}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>` +
      extra
    );
  }

  function buildCardModel(key) {
    const p = presets[simState.preset];
    const light = stage.getAttribute("data-bar-theme") === "light";
    switch (key) {
      case "cpu":
        return {
          right: false, color: light ? "#026aa7" : "#2cc5ff", title: "CPU", meta: "PawnIO package telemetry",
          primary: `${p.cpuPct}% ${p.cpuC}° ${p.cpuW}W`, pct: p.cpuPct,
          sub: `Load ${p.cpuPct}% | ${p.cpuC} °C | ${p.cpuW} W package`,
          chartTitle: "Last 3 minutes", leftAxis: "-3m", rightAxis: "Now",
          pts: [10, 12, 18, 14, 11, 22, 19, 15, p.cpuPct], forecast: false,
        };
      case "ram":
        return {
          right: false, color: light ? "#6d28d9" : "#a87ffb", title: "RAM", meta: "Physical memory",
          primary: `${p.ramPct}%`, pct: p.ramPct, sub: p.ramSub,
          chartTitle: "Last 3 minutes", leftAxis: "-3m", rightAxis: "Now",
          pts: [58, 58, 59, 60, 60, 60, 61, 60, p.ramPct], forecast: false,
        };
      case "vram":
        return {
          right: false, color: light ? "#c2410c" : "#ff7a2f", title: "VRAM", meta: "Dedicated GPU memory",
          primary: `${p.vramPct}%`, pct: p.vramPct, sub: p.vramSub,
          chartTitle: "Last 3 minutes", leftAxis: "-3m", rightAxis: "Now",
          pts: [61, 62, 62, 64, 65, 65, 66, 66, p.vramPct], forecast: false,
        };
      case "ssd":
        return {
          right: false, color: light ? "#9a6700" : "#eab308", title: "Storage", meta: "IOCTL_STORAGE_QUERY_PROPERTY",
          primary: `${p.ssdPct}% ${p.ssdC}°C`, pct: Math.max(8, p.ssdPct), sub: p.ssdSub,
          chartTitle: "Last 3 minutes", leftAxis: "-3m", rightAxis: "Now",
          pts: [2, 5, 3, 1, 8, 2, 1, 0, p.ssdPct], forecast: false,
        };
      case "net":
        return {
          right: false, color: light ? "#0067c0" : "#38bdf8", title: "Network", meta: "GetIfTable2 counters",
          primary: p.netPrimary, pct: p.netPct, sub: p.netSub,
          chartTitle: "Last 3 minutes", leftAxis: "-3m", rightAxis: "Now",
          pts: p.netPts, forecast: false,
        };
      case "sysbat":
        return {
          right: false, color: light ? "#0f766e" : "#2dd4bf", title: "Laptop Battery", meta: "SystemBatteryState",
          primary: p.batPrimary, pct: p.batPct, sub: p.batSub,
          chartTitle: "Last 3 minutes", leftAxis: "-3m", rightAxis: "Now",
          pts: [92, 91, 91, 90, 90, 89, 89, 88, p.batPct], forecast: false,
        };
      case "headset":
        return {
          right: true, color: light ? "#15803d" : "#44d62c", title: "Razer Barracuda X (2022)", meta: "2.4 GHz dongle",
          primary: "45%", pct: 45, sub: "Estimated time to empty: ~5 d 8 h",
          chartTitle: "Last 24 hours and forecast", leftAxis: "24h ago", rightAxis: "~5 d 8 h",
          pts: [54, 52, 50, 49, 48, 47, 46, 45], forecast: true,
        };
      case "mouse":
        return {
          right: true, color: light ? "#15803d" : "#44d62c", title: "Razer Viper V3 Pro", meta: "Razer receiver",
          primary: "35%", pct: 35, sub: "Estimated time to empty: ~3 d 20 h (approx.)",
          chartTitle: "Last 24 hours and forecast", leftAxis: "24h ago", rightAxis: "~3 d 20 h",
          pts: [50, 48, 47, 45, 42, 40, 38, 35], forecast: true,
        };
      case "keyboard":
        return {
          right: true, color: light ? "#0f7b0f" : "#34d399", title: "AULA wireless keyboard", meta: "2.4 GHz receiver",
          primary: "100%", pct: 100, sub: "Full charge recorded | 365-day health tracking active",
          chartTitle: "Last 24 hours", leftAxis: "24h ago", rightAxis: "Now",
          pts: [92, 95, 100, 100, 100, 100, 100, 100], forecast: false,
        };
      case "phone":
        return {
          right: true, color: light ? "#0067c0" : "#4cc2ff", title: "Android Phone (Phone Link)", meta: "Charging",
          primary: "29% charging", pct: 29, sub: "Windows Phone Link cache | Notify limit: 85%",
          chartTitle: "Last 24 hours", leftAxis: "24h ago", rightAxis: "Now",
          pts: [48, 40, 32, 24, 18, 22, 25, 29], forecast: false,
        };
      case "gpu":
      default:
        return {
          right: false, color: light ? "#15803d" : "#3ddc68", title: "GPU", meta: "NVIDIA GeForce RTX 4050 Laptop GPU",
          primary: `${p.gpuPct}% ${p.gpuC}° ${p.gpuW}W ${p.gpuClock}MHz`, pct: p.gpuPct,
          sub: `Clocks: ${p.gpuClock} / ${p.gpuMemClock} MHz | ${p.gpuPct}% · ${p.gpuW} W`,
          chartTitle: "Last 3 minutes", leftAxis: "-3m", rightAxis: "Now",
          pts: [8, 12, 28, 14, 15, 22, 25, 21, p.gpuPct], forecast: false,
        };
    }
  }

  function renderCard() {
    const model = buildCardModel(simState.inspected);
    stageTop.classList.toggle("is-right", model.right || simState.layout === "combined");
    cardDot.style.background = model.color;
    cardTitle.textContent = model.title;
    cardMeta.textContent = model.meta;
    cardPrimary.textContent = model.primary;
    cardFill.style.width = `${model.pct}%`;
    cardFill.style.background = model.color;
    cardSub.textContent = model.sub;
    cardChartTitle.textContent = model.chartTitle;
    cardAxis.innerHTML = `<span>${model.leftAxis}</span><span>${model.rightAxis}</span>`;
    cardSvg.innerHTML = buildSparklineSvg(model.pts, model.color, model.forecast);
  }

  function renderSim() {
    const p = presets[simState.preset];
    valCpu.textContent = `${p.cpuPct}% ${p.cpuC}° ${p.cpuW}W`;
    valRam.textContent = `${p.ramPct}%`;
    valGpu.textContent = `${p.gpuPct}% ${p.gpuC}° ${p.gpuW}W ${p.gpuClock}MHz`;
    valVram.textContent = `${p.vramPct}%`;
    valSsd.textContent = `${p.ssdPct}% ${p.ssdC}°`;
    valNet.textContent = p.netText;
    valSysBat.textContent = p.batStrip;

    barCpu.style.width = `${p.cpuPct}%`;
    barRam.style.width = `${p.ramPct}%`;
    barGpu.style.width = `${p.gpuPct}%`;
    barVram.style.width = `${p.vramPct}%`;
    barSsd.style.width = `${Math.max(8, p.ssdPct)}%`;
    barSysBat.style.width = `${p.batPct}%`;

    itemCpu.classList.remove("heat-warn", "heat-high");
    itemGpu.classList.remove("heat-warn", "heat-high");
    if (p.cpuHeat) itemCpu.classList.add(p.cpuHeat);
    if (p.gpuHeat) itemGpu.classList.add(p.gpuHeat);

    stage.setAttribute("data-style", simState.style);
    taskbar.setAttribute("data-layout", simState.layout);

    const rightBox = document.querySelector(".taskbar-right");
    const centerIcons = document.querySelector(".taskbar-center-icons");
    if (simState.layout === "combined") {
      rightBox.insertBefore(stripSys, stripBat);
      if (centerIcons) centerIcons.style.marginInline = "0";
    } else {
      taskbar.insertBefore(stripSys, taskbar.firstChild);
      if (centerIcons) centerIcons.style.marginInline = "auto";
    }

    renderCard();
  }

  function syncSegGroup(selector, activeBtn) {
    document.querySelectorAll(selector).forEach((b) => {
      const isMatch = b === activeBtn;
      b.classList.toggle("is-active", isMatch);
      b.setAttribute("aria-pressed", String(isMatch));
    });
  }

  document.querySelectorAll("[data-style]").forEach((btn) => {
    btn.addEventListener("click", () => {
      syncSegGroup("[data-style]", btn);
      simState.style = btn.dataset.style;
      renderSim();
    });
  });

  document.querySelectorAll("[data-layout]").forEach((btn) => {
    btn.addEventListener("click", () => {
      syncSegGroup("[data-layout]", btn);
      simState.layout = btn.dataset.layout;
      renderSim();
    });
  });

  document.querySelectorAll("[data-preset]").forEach((btn) => {
    btn.addEventListener("click", () => {
      syncSegGroup("[data-preset]", btn);
      simState.preset = btn.dataset.preset;
      renderSim();
    });
  });

  document.querySelectorAll("[data-bar-theme]").forEach((btn) => {
    btn.addEventListener("click", () => {
      syncSegGroup("[data-bar-theme]", btn);
      stage.setAttribute("data-bar-theme", btn.dataset.barTheme);
      renderCard();
    });
  });

  const inspectButtons = Array.from(document.querySelectorAll("[data-inspect]"));
  inspectButtons.forEach((btn) => {
    const activate = () => {
      inspectButtons.forEach((b) => b.classList.remove("is-inspected"));
      btn.classList.add("is-inspected");
      simState.inspected = btn.dataset.inspect;
      renderCard();
    };
    btn.addEventListener("mouseenter", activate);
    btn.addEventListener("focus", activate);
    btn.addEventListener("click", activate);
  });

  renderSim();

  // Accessible vertical tablist keyboard helper (ArrowUp / ArrowDown / Home / End)
  function wireTablist(listSelector, activateFn) {
    const list = document.querySelector(listSelector);
    if (!list) return;
    const tabs = Array.from(list.querySelectorAll('[role="tab"]'));
    const selectTab = (tab, moveFocus) => {
      const panelId = tab.getAttribute("aria-controls");
      const panel = panelId ? document.getElementById(panelId) : null;
      tabs.forEach((t) => {
        const active = t === tab;
        t.classList.toggle("is-active", active);
        t.setAttribute("aria-selected", String(active));
        t.setAttribute("tabindex", active ? "0" : "-1");
      });
      if (panel && tab.id) panel.setAttribute("aria-labelledby", tab.id);
      if (moveFocus) tab.focus();
      activateFn(tab);
    };

    tabs.forEach((tab, idx) => {
      tab.addEventListener("click", () => selectTab(tab, false));
      tab.addEventListener("keydown", (e) => {
        let nextIdx = -1;
        if (e.key === "ArrowDown" || e.key === "ArrowRight") nextIdx = (idx + 1) % tabs.length;
        else if (e.key === "ArrowUp" || e.key === "ArrowLeft") nextIdx = (idx - 1 + tabs.length) % tabs.length;
        else if (e.key === "Home") nextIdx = 0;
        else if (e.key === "End") nextIdx = tabs.length - 1;
        if (nextIdx >= 0) {
          e.preventDefault();
          selectTab(tabs[nextIdx], true);
        }
      });
    });
  }

  // 3. Taskbar Layout & Hover Card Capture Switcher
  const capImg = document.getElementById("capture-img");
  const capImg2 = document.getElementById("capture-img-secondary");
  const capText = document.getElementById("capture-cap");
  const capturesMeta = {
    "strip-vitals": {
      src: "docs/screenshots/strip-vitals.png",
      alt: "1-row Windows 11 System Vitals strip with colored vector icons and progress bars",
      src2: "docs/screenshots/strip-batteries.png",
      alt2: "1-row Windows 11 Wireless Batteries strip beside the clock",
      cap: "<strong>1-Row Dual Strips:</strong> System telemetry sits on the left end while headset, mouse, keyboard, and Phone Link batteries anchor next to the notification area.",
    },
    "strip-2row": {
      src: "docs/screenshots/strip-vitals-2row.png",
      alt: "Compact 2-row Windows 11 System Vitals taskbar layout",
      src2: "",
      alt2: "",
      cap: "<strong>Compact 2-Row Layout:</strong> Fits all 6–7 system metrics into roughly half the horizontal space.",
    },
    "strip-stacked": {
      src: "docs/screenshots/strip-vitals-stacked.png",
      alt: "Stacked System Vitals taskbar strip with vector icons above numbers",
      src2: "docs/screenshots/strip-batteries-stacked.png",
      alt2: "Stacked Wireless Batteries strip with icons above percentages",
      cap: "<strong>Icons Above Values (Narrower):</strong> Vector glyphs sit neatly above values across both strips, saving width for centered Windows 11 app icons.",
    },
    "strip-ring": {
      src: "docs/screenshots/strip-batteries-ring.png",
      alt: "Radial circular battery ring gauges beside the Windows 11 clock",
      src2: "",
      alt2: "",
      cap: "<strong>Radial Ring Style:</strong> Replaces text percentages with clean circular arc gauges and blue charging arcs beside the notification clock.",
    },
    "hover-gpu": {
      src: "docs/screenshots/hover-gpu.png",
      alt: "GPU rich hover card showing 3-minute sparkline and 2655/8001 MHz clocks",
      src2: "",
      alt2: "",
      cap: "<strong>3-Minute System Sparkline Card:</strong> Hovering GPU shows adapter name, core/VRAM clocks (2655/8001 MHz), load bar, and live 3-minute waveform.",
    },
    "hover-device": {
      src: "docs/screenshots/hover-device.png",
      alt: "Razer Viper V3 Pro rich hover card showing 24-hour discharge curve and ~3 d 20 h runtime forecast",
      src2: "",
      alt2: "",
      cap: "<strong>24-Hour Discharge &amp; Forecast Card:</strong> Hovering any peripheral plots 24-hour battery discharge alongside a conservative time-to-empty dashed forecast.",
    },
  };

  wireTablist(".capture-selector", (btn) => {
    const info = capturesMeta[btn.dataset.capture];
    if (!info) return;
    capImg.src = info.src;
    capImg.alt = info.alt;
    if (info.src2) {
      capImg2.src = info.src2;
      capImg2.alt = info.alt2;
      capImg2.hidden = false;
    } else {
      capImg2.hidden = true;
    }
    capText.innerHTML = info.cap;
  });

  // 4. Device Dashboard View Switcher
  const dashImg = document.getElementById("dash-img");
  const dashCap = document.getElementById("dash-cap");
  const dashMeta = {
    overview: {
      src: "docs/screenshots/dashboard-overview.png",
      alt: "Taskbar Vitals Device Dashboard Overview tab",
      cap: "<strong>Overview tab:</strong> Live device cards with brand colors, micro-sparklines, and connection status across mice, keyboards, and headsets.",
    },
    comparison: {
      src: "docs/screenshots/dashboard-comparison.png",
      alt: "Taskbar Vitals Battery Consumption Comparison tab showing percent-per-day drain rate and estimated full runtime",
      cap: "<strong>Battery Comparison tab:</strong> Compare peripheral efficiency side-by-side in observed <code>%/day</code> drain rate and full-charge runtime.",
    },
    details: {
      src: "docs/screenshots/dashboard-details.png",
      alt: "Taskbar Vitals Device Details tab with 7-day trend, runtime estimate, battery health, and charging sessions",
      cap: "<strong>Device Details &amp; Health:</strong> Inspect 7-day discharge trends, full-charge runtime estimates, 365-day wear health, and recent charging sessions.",
    },
  };

  wireTablist(".dash-switcher", (btn) => {
    const info = dashMeta[btn.dataset.dash];
    if (!info) return;
    dashImg.src = info.src;
    dashImg.alt = info.alt;
    dashCap.innerHTML = info.cap;
  });

  // 5. Settings Panel Tab Switcher
  const panelShot = document.getElementById("panel-shot-img");
  const panelCap = document.getElementById("panel-shot-cap");
  const tabMeta = {
    system: {
      src: "docs/screenshots/panel-system.png",
      alt: "Settings Panel System tab showing CPU, RAM, and GPU clock toggles",
      cap: "<strong>System tab:</strong> Live header pills (<code>12% · 65 °C · 21 W</code>), independent usage / °C / watts / MHz clock switches, and PawnIO helper setup.",
    },
    "system-storage": {
      src: "docs/screenshots/panel-system-storage.png",
      alt: "Settings Panel System tab lower cards showing VRAM, NVMe Storage temperature, Network speed test, and thermal alerts",
      cap: "<strong>VRAM, Storage &amp; Network:</strong> Zero-admin NVMe temperature/GB monitoring, built-in HTTP Speed Test, and high-temperature warning thresholds.",
    },
    batteries: {
      src: "docs/screenshots/panel-batteries.png",
      alt: "Settings Panel Batteries tab showing Device dashboard launch button and visibility controls",
      cap: "<strong>Batteries tab:</strong> Live device rows with <code>↑</code>/<code>↓</code> reorder buttons, per-device <code>⋯</code> menus, <strong>Device dashboard...</strong>, and visibility toggles.",
    },
    "batteries-options": {
      src: "docs/screenshots/panel-batteries-options.png",
      alt: "Settings Panel Visibility and Battery alerts section",
      cap: "<strong>Visibility &amp; Alerts:</strong> Dim sleeping devices, move to tray when empty, record 30-day battery history, and set charge-limit alerts.",
    },
    appearance: {
      src: "docs/screenshots/panel-appearance.png",
      alt: "Settings Panel Appearance tab showing Dual-strip positioning and Style toggles",
      cap: "<strong>Appearance tab:</strong> Independent Dual-strip placement, crisp vector icons, compact 2-row layout, stacked icons, and radial battery rings.",
    },
    general: {
      src: "docs/screenshots/panel-general.png",
      alt: "Settings Panel General tab showing full-screen polling throttle, Local JSON pipe, and Export/Import settings",
      cap: "<strong>General tab:</strong> 21 built-in languages, <strong>Poll less during full-screen games</strong>, Local JSON pipe, signed updates, and Export/Import settings.",
    },
  };

  wireTablist(".panel-tab-list", (btn) => {
    const info = tabMeta[btn.dataset.tab];
    if (!info) return;
    panelShot.src = info.src;
    panelShot.alt = info.alt;
    panelCap.innerHTML = info.cap;
  });

  // 6. Hardware Explorer powered by ./devices.json with full multi-category mapping
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

  const searchInput = document.getElementById("hw-search");
  const clearBtn = document.getElementById("hw-clear");
  const tbody = document.getElementById("hw-tbody");
  const countEl = document.getElementById("hw-count");
  const expandBtn = document.getElementById("hw-expand-btn");
  const matrixLink = document.getElementById("hw-matrix-link");
  const catLabels = {
    mice: "Mice",
    keyboards: "Keyboards",
    audio: "Audio",
    controllers: "Controllers",
    mixed: "Mixed",
    fallbacks: "Fallbacks",
  };
  let devices = [];
  let activeCat = "all";
  let expanded = false;

  function matchesCategoryFilter(entry, cat) {
    if (cat === "all") return true;
    if (entry.category === cat) return true;
    const mapped = MIXED_CATEGORY_MAP[entry.vendor];
    if (Array.isArray(mapped) && mapped.includes(cat)) return true;
    if (entry.category !== "mixed") return false;
    const text = `${entry.models || ""} ${entry.protocol || ""}`.toLowerCase();
    if (cat === "mice") return /\bmice\b|\bmouse\b/.test(text);
    if (cat === "keyboards") return /\bkeyboards?\b/.test(text);
    if (cat === "audio") return /\bheadsets?\b|\bearbuds?\b|\bheadphones?\b|\bspeaker\b/.test(text);
    if (cat === "controllers") return /\bcontrollers?\b|\bgamepad\b|\bdualsense\b|\bdualshock\b/.test(text);
    return false;
  }

  function renderHardware() {
    const rawQ = searchInput.value.trim();
    clearBtn.hidden = rawQ.length === 0;
    const q = rawQ.toLowerCase();
    const qNoColon = q.replace(/:/g, "");

    if (matrixLink) {
      const params = new URLSearchParams();
      if (rawQ) params.set("q", rawQ);
      if (activeCat !== "all") params.set("cat", activeCat);
      const qs = params.toString();
      matrixLink.href = qs ? `compatibility.html?${qs}` : "compatibility.html";
    }

    if (devices.length === 0) {
      tbody.innerHTML = "";
      expandBtn.style.display = "none";
      return;
    }

    const filtered = devices.filter((entry) => {
      if (!matchesCategoryFilter(entry, activeCat)) return false;
      if (!q) return true;
      const vidStr = (entry.vidPids || []).join(" ");
      const hay = `${entry.vendor}\n${entry.models}\n${entry.protocol}\n${vidStr}`.toLowerCase();
      return hay.includes(q) || (qNoColon.length >= 4 && hay.replace(/:/g, "").includes(qNoColon));
    });

    const visible = expanded || q.length > 0 ? filtered : filtered.slice(0, 12);
    tbody.innerHTML = "";

    if (visible.length === 0) {
      const tr = document.createElement("tr");
      const td = document.createElement("td");
      td.colSpan = 3;
      td.textContent = "No matching hardware families found. Try another keyword or VID:PID, or run TaskbarVitals.exe --report.";
      tr.appendChild(td);
      tbody.appendChild(tr);
    } else {
      const frag = document.createDocumentFragment();
      visible.forEach((entry) => {
        const tr = document.createElement("tr");

        const td1 = document.createElement("td");
        td1.className = "hw-brand";
        const brandRow = document.createElement("div");
        brandRow.className = "hw-brand-row";
        const brandName = document.createElement("span");
        brandName.textContent = entry.vendor;
        brandRow.appendChild(brandName);
        if (entry.tested) {
          const testedBadge = document.createElement("span");
          testedBadge.className = "badge-tested";
          testedBadge.textContent = "Tested";
          brandRow.appendChild(testedBadge);
        }
        td1.appendChild(brandRow);

        const td2 = document.createElement("td");
        td2.className = "hw-cat";
        td2.textContent = catLabels[entry.category] || entry.category;

        const td3 = document.createElement("td");
        td3.textContent = String(entry.models || "").replace(/\n+/g, " · ");

        tr.append(td1, td2, td3);
        frag.appendChild(tr);
      });
      tbody.appendChild(frag);
    }

    countEl.textContent = `Showing ${visible.length} of ${filtered.length} matching families (${devices.length} total in devices.json)`;
    if (q.length > 0 || filtered.length <= 12) {
      expandBtn.style.display = "none";
    } else {
      expandBtn.style.display = "";
      expandBtn.textContent = expanded ? "Show compact preview (12)" : `Show all ${filtered.length} matching families`;
    }
  }

  searchInput.addEventListener("input", renderHardware);
  clearBtn.addEventListener("click", () => {
    searchInput.value = "";
    renderHardware();
    searchInput.focus();
  });
  document.querySelectorAll(".filter-btn").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.querySelectorAll(".filter-btn").forEach((b) => {
        const active = b === btn;
        b.classList.toggle("is-active", active);
        b.setAttribute("aria-pressed", String(active));
      });
      activeCat = btn.dataset.cat;
      renderHardware();
    });
  });
  expandBtn.addEventListener("click", () => {
    expanded = !expanded;
    renderHardware();
  });

  fetch("./devices.json")
    .then((res) => (res.ok ? res.json() : null))
    .then((data) => {
      if (!Array.isArray(data)) throw new Error("invalid");
      devices = data;
      renderHardware();
    })
    .catch(() => {
      countEl.textContent =
        "Unable to fetch ./devices.json in this context. Open the hosted compatibility matrix or stage _site/ per .github/workflows/pages.yml.";
      renderHardware();
    });

  // 7. Copy PowerShell Named Pipe snippet (with fallback when Clipboard API is unavailable)
  const copyBtn = document.getElementById("copy-pipe-btn");
  const pipeCode = document.getElementById("pipe-code");
  if (copyBtn && pipeCode) {
    const markCopied = () => {
      copyBtn.textContent = "Copied!";
      setTimeout(() => { copyBtn.textContent = "Copy snippet"; }, 1800);
    };
    copyBtn.addEventListener("click", () => {
      const text = pipeCode.textContent;
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(text).then(markCopied).catch(() => {});
        return;
      }
      const ta = document.createElement("textarea");
      ta.value = text;
      ta.setAttribute("readonly", "");
      ta.style.position = "fixed";
      ta.style.opacity = "0";
      document.body.appendChild(ta);
      ta.select();
      try {
        if (document.execCommand("copy")) markCopied();
      } catch (_) {}
      document.body.removeChild(ta);
    });
  }

  // 8. Hydrate live GitHub latest release assets (defaults cleanly to releases/latest)
  fetch("https://api.github.com/repos/symNistaken/TaskbarVitals/releases/latest")
    .then((r) => (r.ok ? r.json() : null))
    .then((rel) => {
      if (!rel || !Array.isArray(rel.assets)) return;
      const setup = rel.assets.find((a) => /^TaskbarVitals-Setup-.*\.exe$/i.test(a.name));
      const zip = rel.assets.find((a) => /^TaskbarVitals-.*-portable\.zip$/i.test(a.name));
      if (setup) {
        document.getElementById("download-setup-btn").href = setup.browser_download_url;
        document.getElementById("download-setup-label").textContent = `Download Setup ${rel.tag_name}`;
      }
      if (zip) {
        document.getElementById("download-zip-btn").href = zip.browser_download_url;
        const mb = (zip.size / (1024 * 1024)).toFixed(2);
        document.getElementById("download-zip-label").textContent = `Portable ZIP (${mb} MB)`;
      }
      if (rel.tag_name) {
        document.getElementById("live-version").textContent = rel.tag_name;
      }
    })
    .catch(() => {});
})();
