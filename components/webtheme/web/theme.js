"use strict";
// Shared theme for the dongle's pages. Sets the mode (plain, arcade, retro)
// before the page is drawn, remembers it in the browser and handles the
// #theme button, which switches to the next one. grilloLive(true) animates the
// logo while the page is connected.
(function () {
  const KEY = "grillo.theme", MODES = ["plain", "arcade", "retro"], root = document.documentElement;
  let mode = "plain";
  // localStorage may be missing (private browsing, blocked data): stay plain
  try { mode = localStorage.getItem(KEY) || "plain"; } catch (e) {}
  if (!MODES.includes(mode)) mode = "plain";
  const apply = () => { for (const m of MODES) root.classList.toggle(m, m === mode); };
  apply();
  window.grilloLive = on => root.classList.toggle("live", !!on);
  document.addEventListener("DOMContentLoaded", () => {
    const b = document.getElementById("theme");
    if (!b) return;
    const show = () => { b.textContent = mode[0].toUpperCase() + mode.slice(1); b.title = "Theme: " + mode; };
    b.onclick = () => {
      mode = MODES[(MODES.indexOf(mode) + 1) % MODES.length];
      apply();
      try { localStorage.setItem(KEY, mode); } catch (e) {}
      show();
    };
    show();
  });
})();
