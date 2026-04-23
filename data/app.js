const state = {
  config: null,
  selectedProfile: 0,
  selectedButton: 0,
  recording: false,
};

const els = {
  profileTabs: document.getElementById("profileTabs"),
  profileNameInput: document.getElementById("profileNameInput"),
  buttonGrid: document.getElementById("buttonGrid"),
  selectedButtonLabel: document.getElementById("selectedButtonLabel"),
  actionType: document.getElementById("actionType"),
  comboGroup: document.getElementById("comboGroup"),
  comboKeys: document.getElementById("comboKeys"),
  recordComboBtn: document.getElementById("recordComboBtn"),
  textGroup: document.getElementById("textGroup"),
  textData: document.getElementById("textData"),
  mediaGroup: document.getElementById("mediaGroup"),
  mediaData: document.getElementById("mediaData"),
  mouseGroup: document.getElementById("mouseGroup"),
  mouseData: document.getElementById("mouseData"),
  layerGroup: document.getElementById("layerGroup"),
  layerData: document.getElementById("layerData"),
  encoderCw: document.getElementById("encoderCw"),
  encoderCcw: document.getElementById("encoderCcw"),
  encoderPress: document.getElementById("encoderPress"),
  saveBtn: document.getElementById("saveBtn"),
  reloadBtn: document.getElementById("reloadBtn"),
  rebootBtn: document.getElementById("rebootBtn"),
  addProfileBtn: document.getElementById("addProfileBtn"),
  renameProfileBtn: document.getElementById("renameProfileBtn"),
  deleteProfileBtn: document.getElementById("deleteProfileBtn"),
  applyButtonBtn: document.getElementById("applyButtonBtn"),
  toast: document.getElementById("toast"),
  bleStatus: document.getElementById("bleStatus"),
  batteryStatus: document.getElementById("batteryStatus"),
  apStatus: document.getElementById("apStatus"),
};

function defaultConfig() {
  return {
    version: 1,
    active_profile: 0,
    profiles: [createNewProfile(0, "Default")],
  };
}

function createNewProfile(id, name) {
  return {
    id,
    name,
    buttons: Array.from({ length: 9 }, (_, i) => ({ id: i, type: "none" })),
    encoder: {
      cw: "KEY_MEDIA_VOLUME_UP",
      ccw: "KEY_MEDIA_VOLUME_DOWN",
      press: "KEY_MEDIA_MUTE",
    },
  };
}

function toast(msg, isError = false) {
  els.toast.textContent = msg;
  els.toast.style.borderColor = isError ? "rgba(255, 107, 107, 0.8)" : "rgba(79, 209, 197, 0.8)";
  els.toast.classList.add("show");
  clearTimeout(toast._timer);
  toast._timer = setTimeout(() => els.toast.classList.remove("show"), 2200);
}

function profileSafe() {
  if (!state.config) return null;
  if (state.selectedProfile >= state.config.profiles.length) state.selectedProfile = 0;
  return state.config.profiles[state.selectedProfile] || null;
}

function selectedButtonSafe() {
  const profile = profileSafe();
  if (!profile) return null;
  if (state.selectedButton >= profile.buttons.length) state.selectedButton = 0;
  return profile.buttons[state.selectedButton] || null;
}

function actionSummary(action) {
  if (!action || !action.type || action.type === "none") return "None";
  if (action.type === "combo") return (action.keys || []).join(" + ") || "Combo";
  if (action.type === "text") return action.data || "Text";
  return action.data || action.type;
}

function renderProfiles() {
  const profile = profileSafe();
  els.profileTabs.innerHTML = "";

  state.config.profiles.forEach((p, idx) => {
    const tab = document.createElement("button");
    tab.className = "profile-tab" + (idx === state.selectedProfile ? " active" : "");
    tab.textContent = p.name;
    tab.onclick = () => {
      state.selectedProfile = idx;
      state.selectedButton = 0;
      renderAll();
    };
    els.profileTabs.appendChild(tab);
  });

  els.profileNameInput.value = profile ? profile.name : "";
}

function renderGrid() {
  const profile = profileSafe();
  els.buttonGrid.innerHTML = "";
  if (!profile) return;

  profile.buttons.forEach((btn, idx) => {
    const b = document.createElement("button");
    b.className = "grid-btn" + (idx === state.selectedButton ? " selected" : "");
    b.innerHTML = `<strong>K${idx + 1}</strong><small>${escapeHtml(actionSummary(btn))}</small>`;
    b.onclick = () => {
      state.selectedButton = idx;
      renderAll();
    };
    els.buttonGrid.appendChild(b);
  });
}

function renderButtonEditor() {
  const btn = selectedButtonSafe();
  const profile = profileSafe();
  if (!btn || !profile) return;

  els.selectedButtonLabel.textContent = `Editing ${profile.name} • Button ${btn.id + 1}`;
  els.actionType.value = btn.type || "none";

  els.comboKeys.value = (btn.keys || []).join(",");
  els.textData.value = btn.data || "";
  els.mediaData.value = btn.data || "KEY_MEDIA_VOLUME_UP";
  els.mouseData.value = btn.data || "MOUSE_SCROLL_UP";
  els.layerData.value = Number.isInteger(Number(btn.data)) ? String(btn.data) : "0";

  toggleEditorGroups(btn.type || "none");
}

function renderEncoderEditor() {
  const profile = profileSafe();
  if (!profile) return;

  els.encoderCw.value = profile.encoder?.cw || "";
  els.encoderCcw.value = profile.encoder?.ccw || "";
  els.encoderPress.value = profile.encoder?.press || "";
}

function toggleEditorGroups(type) {
  els.comboGroup.classList.toggle("hidden", type !== "combo");
  els.textGroup.classList.toggle("hidden", type !== "text");
  els.mediaGroup.classList.toggle("hidden", type !== "media");
  els.mouseGroup.classList.toggle("hidden", type !== "mouse");
  els.layerGroup.classList.toggle("hidden", type !== "layer_switch");
}

function renderAll() {
  renderProfiles();
  renderGrid();
  renderButtonEditor();
  renderEncoderEditor();
}

function normalizeConfig(cfg) {
  const safe = cfg && cfg.version === 1 ? cfg : defaultConfig();
  if (!Array.isArray(safe.profiles) || safe.profiles.length === 0) {
    safe.profiles = [createNewProfile(0, "Default")];
  }

  safe.active_profile = Math.max(0, Math.min(safe.active_profile || 0, safe.profiles.length - 1));

  safe.profiles = safe.profiles.map((p, idx) => {
    const profile = {
      id: Number.isInteger(p.id) ? p.id : idx,
      name: p.name || `Profile ${idx + 1}`,
      buttons: Array.isArray(p.buttons) ? p.buttons.slice(0, 9) : [],
      encoder: p.encoder || {},
    };

    while (profile.buttons.length < 9) {
      profile.buttons.push({ id: profile.buttons.length, type: "none" });
    }

    profile.buttons = profile.buttons.map((b, i) => ({
      id: Number.isInteger(b.id) ? b.id : i,
      type: b.type || "none",
      keys: Array.isArray(b.keys) ? b.keys.slice(0, 6) : undefined,
      data: typeof b.data === "string" ? b.data : undefined,
    }));

    profile.encoder = {
      cw: profile.encoder.cw || "KEY_MEDIA_VOLUME_UP",
      ccw: profile.encoder.ccw || "KEY_MEDIA_VOLUME_DOWN",
      press: profile.encoder.press || "KEY_MEDIA_MUTE",
    };

    return profile;
  });

  return safe;
}

function readEditorIntoButton() {
  const profile = profileSafe();
  const btn = selectedButtonSafe();
  if (!profile || !btn) return;

  const type = els.actionType.value;
  const next = { id: btn.id, type };

  if (type === "combo") {
    const keys = els.comboKeys.value
      .split(",")
      .map((k) => k.trim())
      .filter(Boolean)
      .slice(0, 6);
    next.keys = keys;
  } else if (type === "text") {
    next.data = els.textData.value;
  } else if (type === "media") {
    next.data = els.mediaData.value;
  } else if (type === "mouse") {
    next.data = els.mouseData.value;
  } else if (type === "layer_switch") {
    next.data = String(Math.max(0, Number(els.layerData.value) || 0));
  }

  profile.buttons[state.selectedButton] = next;
}

function readEncoderEditor() {
  const profile = profileSafe();
  if (!profile) return;

  profile.encoder = {
    cw: els.encoderCw.value.trim() || "KEY_MEDIA_VOLUME_UP",
    ccw: els.encoderCcw.value.trim() || "KEY_MEDIA_VOLUME_DOWN",
    press: els.encoderPress.value.trim() || "KEY_MEDIA_MUTE",
  };
}

function escapeHtml(v) {
  return String(v)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function keyToToken(ev) {
  const key = ev.key;
  if (key.length === 1 && /[a-zA-Z0-9]/.test(key)) return `KEY_${key.toUpperCase()}`;
  if (key === " ") return "KEY_SPACE";

  const map = {
    Enter: "KEY_ENTER",
    Escape: "KEY_ESC",
    Backspace: "KEY_BACKSPACE",
    Tab: "KEY_TAB",
    Delete: "KEY_DELETE",
    ArrowUp: "KEY_UP_ARROW",
    ArrowDown: "KEY_DOWN_ARROW",
    ArrowLeft: "KEY_LEFT_ARROW",
    ArrowRight: "KEY_RIGHT_ARROW",
    PageUp: "KEY_PAGE_UP",
    PageDown: "KEY_PAGE_DOWN",
    Home: "KEY_HOME",
    End: "KEY_END",
  };

  if (map[key]) return map[key];
  if (/^F\d{1,2}$/.test(key)) return `KEY_${key}`;
  return null;
}

function startRecordCombo() {
  state.recording = true;
  els.recordComboBtn.textContent = "Recording...";

  toast("Hold your keys together, then release"); 


  const downHandler = (ev) => {
    if (!state.recording) return;
    ev.preventDefault();

    const keys = [];
    

    if (ev.ctrlKey) keys.push("KEY_LEFT_CTRL");
    if (ev.shiftKey) keys.push("KEY_LEFT_SHIFT");
    if (ev.altKey) keys.push("KEY_LEFT_ALT");
    if (ev.metaKey) keys.push("KEY_LEFT_GUI"); 


    const isModifier = ["Control", "Shift", "Alt", "Meta"].includes(ev.key);
    if (!isModifier) {
      const main = keyToToken(ev);
      if (main && !keys.includes(main)) keys.push(main);
    }

    els.comboKeys.value = keys.join(",");
  };


  const upHandler = (ev) => {
    if (!state.recording) return;
    ev.preventDefault();
    
    state.recording = false;
    els.recordComboBtn.textContent = "Record";
    

    window.removeEventListener("keydown", downHandler, true);
    window.removeEventListener("keyup", upHandler, true);
  };


  window.addEventListener("keydown", downHandler, true);
  window.addEventListener("keyup", upHandler, true);
}

function buildSavePayload() {
  readEditorIntoButton();
  readEncoderEditor();

  const profile = profileSafe();
  if (profile) {
    profile.name = els.profileNameInput.value.trim() || profile.name;
  }

  state.config.active_profile = state.selectedProfile;
  return state.config;
}

async function loadConfig() {
  try {
    const res = await fetch("/api/config", { cache: "no-store" });
    if (!res.ok) throw new Error("Config request failed");
    const cfg = await res.json();
    state.config = normalizeConfig(cfg);
    state.selectedProfile = state.config.active_profile || 0;
    state.selectedButton = 0;
    renderAll();
    toast("Config loaded");
  } catch (err) {
    console.error(err);
    state.config = defaultConfig();
    renderAll();
    toast("Using local defaults", true);
  }
}


async function restartDevice() {

  if (!confirm("Are you sure you want to restart the Macropad?")) return;
  
  toast("Restarting Macropad...");
  try {
    await fetch("/api/restart", { method: "POST" });
    

    setTimeout(() => {
      els.apStatus.textContent = "AP: Rebooting...";
      els.bleStatus.textContent = "BLE: Disconnected";
      toast("Device restarted. You can close this page.", false);
    }, 1000);
  } catch (err) {
    console.error("Restart trigger failed", err);
    toast("Failed to restart", true);
  }
}

async function saveConfig() {
  const payload = buildSavePayload();
  try {
    const res = await fetch("/api/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });

    const body = await res.json().catch(() => ({}));
    if (!res.ok) {
      throw new Error(body.error || `Save failed (${res.status})`);
    }

    toast("Saved to device");
  } catch (err) {
    console.error(err);
    toast(err.message || "Save failed", true);
  }
}

async function pollStatus() {
  try {
    const res = await fetch("/api/status", { cache: "no-store" });
    if (!res.ok) return;
    const data = await res.json();

    els.bleStatus.textContent = `BLE: ${data.ble_connected ? "Connected" : "Waiting"}`;
    const batteryVoltage = Number(data.battery_voltage ?? data.battery ?? 0);
    const batteryPercent = Number(data.battery_percent ?? 0);
    const charging = Boolean(data.charging);
const batteryLevelEl = document.getElementById("batteryLevel");
    const batteryTextEl = document.getElementById("batteryText");


    if (batteryLevelEl && batteryTextEl) {
      batteryLevelEl.style.width = `${batteryPercent}%`;

      batteryTextEl.textContent = `${batteryPercent}% ${charging ? "⚡" : ""}`;


      if (batteryPercent > 70) {
        batteryLevelEl.style.backgroundColor = "#4fd1c5"; 
      } else if (batteryPercent > 20) {
        batteryLevelEl.style.backgroundColor = "#ffb454"; 
      } else {
        batteryLevelEl.style.backgroundColor = "#ff6b6b"; 
      }
    }
    els.apStatus.textContent = `AP: ${data.ap_enabled ? "On" : "Off"}`;
  } catch (err) {
    console.error(err);
  }
}

function bindUi() {
  els.actionType.addEventListener("change", () => toggleEditorGroups(els.actionType.value));
  els.recordComboBtn.addEventListener("click", startRecordCombo);
  els.applyButtonBtn.addEventListener("click", () => {
    readEditorIntoButton();
    renderGrid();
    toast("Button mapping updated");
  });

  els.addProfileBtn.addEventListener("click", () => {
    const id = state.config.profiles.length;
    state.config.profiles.push(createNewProfile(id, `Profile ${id + 1}`));
    state.selectedProfile = id;
    state.selectedButton = 0;
    renderAll();
  });

  els.renameProfileBtn.addEventListener("click", () => {
    const profile = profileSafe();
    if (!profile) return;
    const value = els.profileNameInput.value.trim();
    if (!value) {
      toast("Profile name cannot be empty", true);
      return;
    }
    profile.name = value;
    renderProfiles();
    toast("Profile renamed");
  });

  els.deleteProfileBtn.addEventListener("click", () => {
    if (state.config.profiles.length <= 1) {
      toast("At least one profile is required", true);
      return;
    }
    state.config.profiles.splice(state.selectedProfile, 1);
    state.config.profiles.forEach((p, idx) => (p.id = idx));
    state.selectedProfile = Math.max(0, state.selectedProfile - 1);
    state.selectedButton = 0;
    renderAll();
    toast("Profile deleted");
  });
  els.saveBtn.addEventListener("click", saveConfig);
  els.rebootBtn.addEventListener("click", restartDevice);
  els.reloadBtn.addEventListener("click", loadConfig);
}

(async function init() {
  bindUi();
  await loadConfig();
  await pollStatus();
  setInterval(pollStatus, 2000);
})();
