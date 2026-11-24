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

  // 2. Interactive Windows Taskbar simulator
  const stage = document.getElementById("desktop-stage");
  const taskbar = document.getElementById("taskbar-preview");
  const stripSys = document.getElementById("strip-system");
  const stripBat = document.getElementById("strip-batteries");
  const tooltip = document.getElementById("sim-tooltip");
  const valCpu = document.getElementById("val-cpu");
  const valGpu = document.getElementById("val-gpu");
  const valRam = document.getElementById("val-ram");
  const itemCpu = document.getElementById("item-cpu");
  const itemGpu = document.getElementById("item-gpu");
  const ttCpu = document.getElementById("tt-cpu");
  const ttGpu = document.getElementById("tt-gpu");
  const ttRam = document.getElementById("tt-ram");

  const simState = {
    preset: "normal",
    layout: "dual",
    watts: true,
    temps: true,
    brands: true,
  };

  const presets = {
    normal: {
      cpuPct: 11, cpuC: 61, cpuW: 20, cpuHeat: "",
      gpuPct: 27, gpuC: 59, gpuW: 15, gpuHeat: "",
      ramPct: 57, ramUsed: "13.6 / 23.8 GB",
    },
    elevated: {
      cpuPct: 68, cpuC: 76, cpuW: 48, cpuHeat: "",
      gpuPct: 88, gpuC: 83, gpuW: 112, gpuHeat: "heat-warn",
      ramPct: 71, ramUsed: "16.9 / 23.8 GB",
    },
    high: {
      cpuPct: 96, cpuC: 92, cpuW: 78, cpuHeat: "heat-high",
      gpuPct: 99, gpuC: 86, gpuW: 138, gpuHeat: "heat-warn",
      ramPct: 84, ramUsed: "20.0 / 23.8 GB",
    },
  };

  function renderSim() {
    const p = presets[simState.preset];
    const cpuParts = [`${p.cpuPct}%`];
    const gpuParts = [`${p.gpuPct}%`];
    if (simState.temps) {
      cpuParts.push(`${p.cpuC}°`);
      gpuParts.push(`${p.gpuC}°`);
    }
    if (simState.watts) {
      cpuParts.push(`${p.cpuW}W`);
      gpuParts.push(`${p.gpuW}W`);
    }
    valCpu.textContent = cpuParts.join(" ");
    valGpu.textContent = gpuParts.join(" ");
    valRam.textContent = `${p.ramPct}%`;

    itemCpu.classList.remove("heat-warn", "heat-high");
    itemGpu.classList.remove("heat-warn", "heat-high");
    if (simState.temps && p.cpuHeat) itemCpu.classList.add(p.cpuHeat);
    if (simState.temps && p.gpuHeat) itemGpu.classList.add(p.gpuHeat);

    ttCpu.textContent = `${p.cpuPct}%, ${p.cpuC} °C, ${p.cpuW} W`;
    ttGpu.textContent = `${p.gpuPct}%, ${p.gpuC} °C, ${p.gpuW} W`;
    ttRam.textContent = `${p.ramPct}% (${p.ramUsed})`;

    stage.classList.toggle("no-brands", !simState.brands);

    const rightBox = document.querySelector(".taskbar-right");
    if (simState.layout === "combined") {
      rightBox.insertBefore(stripSys, stripBat);
    } else {
      taskbar.insertBefore(stripSys, taskbar.firstChild);
    }
  }

  document.querySelectorAll("[data-preset]").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.querySelectorAll("[data-preset]").forEach((b) => b.classList.remove("is-active"));
      btn.classList.add("is-active");
      simState.preset = btn.dataset.preset;
      renderSim();
    });
  });

  document.querySelectorAll("[data-layout]").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.querySelectorAll("[data-layout]").forEach((b) => b.classList.remove("is-active"));
      btn.classList.add("is-active");
      simState.layout = btn.dataset.layout;
      renderSim();
    });
  });

  document.querySelectorAll("[data-bar-theme]").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.querySelectorAll("[data-bar-theme]").forEach((b) => b.classList.remove("is-active"));
      btn.classList.add("is-active");
      stage.setAttribute("data-bar-theme", btn.dataset.barTheme);
    });
  });

  document.querySelectorAll("[data-toggle]").forEach((chk) => {
    chk.addEventListener("change", () => {
      simState[chk.dataset.toggle] = chk.checked;
      renderSim();
    });
  });

  const showTip = () => tooltip.classList.add("is-visible");
  const hideTip = () => tooltip.classList.remove("is-visible");
  stripSys.addEventListener("mouseenter", showTip);
  stripSys.addEventListener("mouseleave", hideTip);
  stripSys.addEventListener("focus", showTip);
  stripSys.addEventListener("blur", hideTip);

  renderSim();

  // 3. Settings Panel Tab Switcher
  const panelShot = document.getElementById("panel-shot-img");
  const panelCap = document.getElementById("panel-shot-cap");
  const tabMeta = {
    system: {
      src: "docs/screenshots/panel-system.png",
      alt: "Taskbar Vitals Settings Panel - System tab screenshot",
      cap: "<strong>System tab:</strong> Live summary pills (<code>18% · 67 °C · 22 W</code>) appear directly on card headers alongside independent toggles and CPU helper setup.",
    },
    batteries: {
      src: "docs/screenshots/panel-batteries.png",
      alt: "Taskbar Vitals Settings Panel - Batteries tab screenshot",
      cap: "<strong>Batteries tab:</strong> Live device rows with <code>↑</code>/<code>↓</code> reorder buttons, per-device <code>⋯</code> action menu, and individual strip visibility switches.",
    },
    "batteries-options": {
      src: "docs/screenshots/panel-batteries-options.png",
      alt: "Taskbar Vitals Settings Panel - Visibility and Battery alerts options",
      cap: "<strong>Visibility &amp; Alerts:</strong> Dim sleeping devices, move to tray when no devices are active, record 30-day battery history, and set charge-limit alerts.",
    },
    appearance: {
      src: "docs/screenshots/panel-appearance.png",
      alt: "Taskbar Vitals Settings Panel - Appearance tab screenshot",
      cap: "<strong>Appearance tab:</strong> Switch between taskbar strip and tray icons, assign Left end vs Next to the clock positions independently, and enable 15 brand colors.",
    },
    general: {
      src: "docs/screenshots/panel-general.png",
      alt: "Taskbar Vitals Settings Panel - General tab screenshot",
      cap: "<strong>General tab:</strong> Pick from 20 languages, launch with Windows, trigger signed updates, prefill device support reports, or open Buy Me a Coffee.",
    },
  };

  document.querySelectorAll(".panel-tab-btn").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.querySelectorAll(".panel-tab-btn").forEach((b) => {
        b.classList.remove("is-active");
        b.setAttribute("aria-selected", "false");
      });
      btn.classList.add("is-active");
      btn.setAttribute("aria-selected", "true");
      const info = tabMeta[btn.dataset.tab];
      if (!info) return;
      panelShot.src = info.src;
      panelShot.alt = info.alt;
      panelCap.innerHTML = info.cap;
    });
  });

  // 4. Searchable Hardware Matrix
  const hwData = [
    ["8BitDo", "controllers", "Ultimate 2 (2DC8:310B / 6012), Ultimate 2C, 3-mode, Pro 3/2, SN30/SF30 Pro · FF7A/FFA0 & SDL"],
    ["AKG", "audio", "N9 Hybrid USB dongle (0ECB:20D9, tested) · Airoha RACE 0CD6"],
    ["Akko", "mice", "Akko Air 01 (3151:5006), V9 Dash Pro (38EE:000C), Dash V9 Ultra (38EE:000F) · FF1C:0092 / Rongyuan"],
    ["Alienware", "audio", "AW920H 2.4 GHz dongle (413C:A515, tested) + Bluetooth fallback"],
    ["Apple AirPods / Beats", "audio", "AirPods Max (200A/201F), AirPods Pro 1-3 (2027 tested), AirPods 1-5, Beats · BLE proximity"],
    ["Arbiter Studio", "mice", "Akitsu (373E:000A/0009), Akitsu 3950, Akitsu Medium (373E:0067/0066 tested)"],
    ["ASTRO", "audio", "A50 X (046D:0B0B), A50 Gen 5, Gen 4 (9886:002C), Gen 3 (9886:0015), A20 Wireless base stations"],
    ["ASUS ROG", "mice", "Azoth, Falchion, Scope II 96; Keris, Harpe, Gladius, Chakram, Spatha; Pelta, Cetra Open, Omni"],
    ["ATK / VXE / VGN", "mice", "VXE R1/Pro Max/SE+/NK, ATK F1/Zero/8K Nano; V75X, Z87, V98 Pro keyboards; N9 Ultra, Mercury, OMNISOUND"],
    ["Attack Shark & Shared", "mice", "Rongyuan (81 models incl. R3, AJ179 Pro, K86), Xenta (X6/X11/X3 Max), Semico 1A2C (X98, AK820 Max Plus)"],
    ["Audeze", "audio", "Maxwell (3329:4B19/4B1A) & Maxwell 2 (3329:4B29 tested) · Airoha RACE read-only"],
    ["AULA", "keyboards", "F75/F87 Pro/F99/F108 Pro, HERO 68 Air (372E:106C); X9 Ultra (320F:22A0), SC580/X; A7Pro headset"],
    ["BEITONG", "controllers", "KP20 (20BC:5128 tested) & KP40D (20BC:515C) in XInput mode · 00FF report 02"],
    ["CHERRY / XTRFY", "keyboards", "MX 3.0S Wireless (046A:00EA/00E3 tested) & Cherry XTRFY 1K mouse (046A:0330 tested)"],
    ["Cooler Master", "mice", "MM712 (tested) & MM731 over 2.4 GHz dongle or USB cable"],
    ["Corsair", "audio", "Void/HS70/HS80/Virtuoso, Slipstream mice & keyboards (1B1C:2B00/1BDC), Sabre V2 Pro (1B1C:2B2A/2B34)"],
    ["DAREU", "mice", "EM901X (260D:1074/1090), A980 Pro (1131/1148), A950 Wing (1154/1196), A84 & EK75 Pro (0042/0046)"],
    ["DARKRIM", "mice", "SHADOW (3654:6901 tested) · CRC-checked 10% step battery query"],
    ["Darmoshark", "mice", "M3 (248A:FF30), M3S (248A:FF31), M3S Pro Nordic 1K (1915:0723)"],
    ["ENDORFY", "audio", "Viro Wireless 2.4 GHz dongle (3738:0072) · Airoha RACE 0CD6"],
    ["Endgame Gear", "mice", "OP1w 4k v1/v2, XM2w 4k v1/v2 (4K dongle), XM2we (3367:1961)"],
    ["EWEADN", "mice", "S7 Max/Pro/Ultra & S9 Max/Pro/Ultra+ (372E:103D receiver, 372E:1027 cable)"],
    ["Fantech", "mice", "Helios Go XD5 (25A7:FA7B/FA7C tested), Raigor III (1EA7:0066 tested), Raigor II WG10"],
    ["Finalmouse", "mice", "UltralightX (361D:0100 tested) & Starlight X (361D:0300/0301 tested) dongles"],
    ["Flydigi", "controllers", "Vader 5 Pro (37D7:2401) & Apex 5 (37D7:2501 tested) · 20% steps + charging"],
    ["Fractal Design", "audio", "Scape wireless headset over 2.4 GHz dongle or USB-C cable"],
    ["G-Wolves", "mice", "Fenrir/Pro/Max/Asym, HTX, HTS, HSK, HTR, HT-S2, Lycan, WARG, VUK & 8K dongles"],
    ["GameSir", "controllers", "Nova/Nova 2 Lite, G7 Pro (3537:105E), Cyclone 2, Cyclone Pro (3537:1015+BLE), Super Nova (103E), Tarantula Pro"],
    ["Glorious", "mice", "Model O / O- / D / D- Wireless (tested), Model O 2 / D 2 PRO"],
    ["HECATE", "mice", "G5M PRO (35BB:D3FB tested) & sibling mice on USB cables 35BB:D3FA/D3FC/D3FD"],
    ["Hitscan", "mice", "Hyperlight X, Hyperlight N, Hyperlight"],
    ["HyperX", "audio", "Cloud II/III/III S/Alpha/Alpha 2/Flight S/Stinger Core/Core DTS; Pulsefire Haste (03F0:028E) & Haste 2/Mini/Core"],
    ["Incott", "mice", "G23 Pro 8K (tested), G23 V2 & G24 (093A:522C / 622C)"],
    ["Inphic", "mice", "IN10R MAX / IN10 MAX on KP 8K receiver (1D57:FA65)"],
    ["Jabra", "audio", "All GN Protocol headsets (Evolve2 / Evolve3 tested on Link 390 0B0E:2E51) on USB or Link dongle"],
    ["JBL", "audio", "Quantum 350 (0ECB:206B tested), 360, 520, 610, 650 (tested), 650X, 850, 950, 950X"],
    ["Keychron", "keyboards", "M4/M5/M3/M6/M7/Nape Pro mice (1K/4K/TurboLink/Ultra-Link 8K 3434:D028); K5 Max & QMK boards (BT or patched 2.4G)"],
    ["Kysona & Shared", "mice", "Kysona M600 / Mercury, Chimera V2 (3554:F512 tested), plus shared Pulsar/ATK/Hitscan devices"],
    ["LAMZU", "mice", "Maya X/M, Tachi, Inca, Paro, Thorn, Atlantis Mini/OG Champion, Orcus (1K/4K/8K/cable)"],
    ["Logitech", "mice", "Unifying, Bolt, Lightspeed (incl. PRO X3 046D:C54F), Nano, USB, BT; G533/535/633/733/933/935/PRO X & G435 (0ADF)"],
    ["Machenike", "mice", "L7 Arctic / L7 Mix (248A:FF4C receiver & FF4E cable, both tested)"],
    ["MAONO", "audio", "MAONO PD200W wireless microphone receiver (352F:040E tested)"],
    ["MCHOSE", "mice", "L7/M7/A7/K7/K7 V2 Ultra+ (3837:1014), G3 V2 (4245), A5 V1/V2 Ultra; V9 Pro/Pro 2 headsets; G75 V2/K87 V3/K99 keyboards"],
    ["MOONDROP", "audio", "GM01 Pro headset on 2.4 GHz dongle (35D8:0133 tested) & USB cable"],
    ["Motospeed / Delux / Ajazz", "mice", "Motospeed X6, Delux M900 Pro / M800 Pro, Ajazz AJ179 / AJ159 / AJ139 V2 MC, AJAZZ AK992 (320F:50C3)"],
    ["MSI", "mice", "VERSA 300 ELITE (0DB0:9E22 tested), CLUTCH GM31 (0DB0:0D3C tested) & GM41 Lightweight"],
    ["Niceboy", "mice", "MW400 on 2.4 GHz receiver (248A:FA02 tested)"],
    ["Ninjutso", "mice", "Sora (248A:5B2F), Sora V2 (1915:AE1C/AE11 tested), Ten & Ten Air (093A:EB01/E020 tested)"],
    ["Nothing / CMF", "audio", "Nothing Ear (3a) (tested), Ear (a), Ear (2), Ear, CMF Buds via Bluetooth RFCOMM (L/R/case)"],
    ["NuPhy", "keyboards", "WH80 over USB cable (19F5:A011) or Bluetooth (19F5:324A tested); Air75V2 under SONiX"],
    ["NZXT", "mice", "Lift Elite Wireless (1E71:2101 dongle / 1E71:2129 cable)"],
    ["Phones (Phone Link)", "controllers", "Android phones paired via Microsoft Phone Link over Wi-Fi or Bluetooth (Nothing Phone (4a) Pro tested)"],
    ["PMO / WOBKEY / URX / EVO80", "keyboards", "Wave 75 RGB (36B0:3002/3009 tested), Aurora 80, PMO P75/80, URX Core75, EVO80"],
    ["Poly / Plantronics", "audio", "Voyager 8200 UC & headsets behind BT600 adapter (047F:02EE/02F7)"],
    ["Press Play", "mice", "ICARUS V2 Nearlink (7768:6434 receiver & Bluetooth tested)"],
    ["Pulsar", "mice", "X2 V3 (3710:5402/3402), X2 V3 eS (3710:5403/3406), Feinmann FO1 / 8K (3710:5404) & Kysona-shared"],
    ["Rapoo", "mice", "VT3 Max Gen 2 (tested), VT9 Gen 2 (tested), Nordic 54L VT mice, legacy VT3 Pro & VT1 Pro Max"],
    ["RAWM / SADES", "mice", "Leviathan V4 (1915:2346 tested), ES21/ER21/ES11/G11/SA-* & SADES Dagger (1915:232A-234B)"],
    ["Razer", "mice", "Wireless mice, keyboards & HyperSpeed multi-device dongles; BlackShark V2/V3/V3 X; Barracuda X 2022 & Pro"],
    ["Redragon", "mice", "M602-KS (258A:002F), M916 Pro 1K (3554:F55D), M915RGB-WL (25A7:FA07), M913, G49, K1NG M999 (320F:226F), Horus Pro"],
    ["Royal Kludge (RK)", "mice", "MG6 Pro (2E3C:00A6/00E6 dongle, 00A5/00E5 cable); R75 & RK 2.4G Dongle under SONiX"],
    ["Samsung", "audio", "Galaxy Buds, Buds+, Live, Pro, Buds2/Pro, FE, Core, Buds3/Pro/FE via RFCOMM (L/R/case)"],
    ["Scyrox", "mice", "V12M on 2.4 GHz receiver (395A:0006)"],
    ["Sharkoon", "mice", "Skiller SGM50W (260D:1125)"],
    ["SONiX Keyboards", "keyboards", "AJAZZ AK820 MAX/Pro/AK980 V2, EPOMAKER EA75/P75/Galaxy100, AULA F108 Pro, RK R75, Lofree Flow2, NuPhy Air75V2, etc."],
    ["Sony", "controllers", "DualShock 4, DualSense, DualSense Edge (USB/BT/adapter); INZONE H9 II, H5 & Buds"],
    ["Sprime", "mice", "PM1 (1915:AC1C tested, AD8C receiver, USB cable)"],
    ["SteelSeries", "audio", "Arctis 1/7/7+/9/Pro/Nova 3P/5/7/Pro/Elite (1038:225D/2244), GameBuds; Aerox 3/5/9, Prime, Rival 3/650/Pro"],
    ["Switch Pro-compatible", "controllers", "Controllers presenting as 057E:2009 (GameSir Tarantula Pro in Switch mode tested)"],
    ["Taidu", "mice", "TSG808 on Compx 2.4 GHz receiver (3554:F5D5 tested)"],
    ["Turtle Beach", "audio", "Stealth 700 Gen 3 (10F5:2251/2262 transmitters, 2253 cable)"],
    ["VAXEE", "mice", "4K (3057:1002) & 8K (3057:2002) receivers; ZYGEN NP-01S V2 cable (3057:1011)"],
    ["WLMouse", "mice", "YING Magnesium (36A7:A874 tested), Beast X 8K/Mini/Pro/MAX/V2 (A856), Sword X 8K, Beast G"],
    ["Wraith", "mice", "W1 / W1 Freestyle (093A:522C receiver, 622C cable)"],
    ["Xbox", "controllers", "Xbox Wireless Adapter, USB & Bluetooth controllers + plugged-in headsets (Windows.Gaming.Input + XInput)"],
    ["Zenith", "mice", "Zenith Pro (260D:1114 receiver tested, 260D:1143 cable)"],
    ["ZOWIE", "mice", "EC1/EC2/EC3-CW, DW series (U2-DW, S2-DW, etc.), and U2 on 2.4 GHz receiver"],
    ["Bluetooth & Fast Pair", "audio", "Any device reporting battery in Windows Bluetooth Settings + Google Fast Pair BLE (FE2C) earbuds (L/R/case)"],
    ["Generic HID & HeadsetControl", "audio", "Standard HID Battery Strength (0x06/0x85) + bundled HeadsetControl 4.3.0 fallback"],
  ];

  const catLabels = {
    mice: "Mice & Receivers",
    keyboards: "Keyboards",
    audio: "Headsets & Earbuds",
    controllers: "Controllers & Phones",
  };

  const searchInput = document.getElementById("hw-search");
  const tbody = document.getElementById("hw-tbody");
  const countEl = document.getElementById("hw-count");
  const expandBtn = document.getElementById("hw-expand-btn");
  const matrixLink = document.getElementById("hw-matrix-link");
  let activeCat = "all";
  let expanded = false;

  function renderHardware() {
    const rawQ = searchInput.value.trim();
    const q = rawQ.toLowerCase();
    if (matrixLink) {
      const params = new URLSearchParams();
      if (rawQ) params.set("q", rawQ);
      if (activeCat !== "all") params.set("cat", activeCat);
      const qs = params.toString();
      matrixLink.href = qs ? `compatibility.html?${qs}` : "compatibility.html";
    }
    const filtered = hwData.filter(([brand, cat, notes]) => {
      const catMatch = activeCat === "all" || cat === activeCat;
      if (!catMatch) return false;
      if (!q) return true;
      return (
        brand.toLowerCase().includes(q) ||
        notes.toLowerCase().includes(q) ||
        catLabels[cat].toLowerCase().includes(q)
      );
    });

    const visible = expanded || q.length > 0 ? filtered : filtered.slice(0, 14);
    tbody.innerHTML = "";
    if (visible.length === 0) {
      const tr = document.createElement("tr");
      const td = document.createElement("td");
      td.colSpan = 3;
      td.textContent = "No matching brands or VID:PIDs found. Try searching another keyword or run TaskbarVitals.exe --report.";
      tr.appendChild(td);
      tbody.appendChild(tr);
    } else {
      visible.forEach(([brand, cat, notes]) => {
        const tr = document.createElement("tr");
        const td1 = document.createElement("td");
        td1.className = "hw-brand";
        td1.textContent = brand;
        const td2 = document.createElement("td");
        td2.className = "hw-cat";
        td2.textContent = catLabels[cat];
        const td3 = document.createElement("td");
        td3.textContent = notes;
        tr.append(td1, td2, td3);
        tbody.appendChild(tr);
      });
    }

    countEl.textContent = `Showing ${visible.length} of ${filtered.length} matching families (${hwData.length} total supported)`;
    if (q.length > 0 || filtered.length <= 14) {
      expandBtn.style.display = "none";
    } else {
      expandBtn.style.display = "";
      expandBtn.textContent = expanded ? "Show compact preview (14)" : `Show all ${filtered.length} supported families`;
    }
  }

  searchInput.addEventListener("input", renderHardware);
  document.querySelectorAll(".filter-btn").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.querySelectorAll(".filter-btn").forEach((b) => b.classList.remove("is-active"));
      btn.classList.add("is-active");
      activeCat = btn.dataset.cat;
      renderHardware();
    });
  });
  expandBtn.addEventListener("click", () => {
    expanded = !expanded;
    renderHardware();
  });
  renderHardware();

  // 5. Hydrate live latest release links from GitHub API (falls back gracefully to v2.0.1)
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
