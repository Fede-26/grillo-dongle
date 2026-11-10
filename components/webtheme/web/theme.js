"use strict";
// Tema comune delle pagine del dongle. Imposta la modalità (plain, arcade,
// retro) prima che la pagina si disegni, la ricorda nel browser e gestisce il
// pulsante #theme, che passa alla successiva. grilloLive(true) anima il logo
// quando la pagina è collegata.
(function () {
  const KEY = "grillo.theme", MODES = ["plain", "arcade", "retro"], root = document.documentElement;
  let mode = "plain";
  // localStorage può mancare (navigazione privata, dati bloccati): resta plain
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
