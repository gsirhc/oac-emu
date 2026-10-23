// Theme bootstrap, run before first paint. Inlined copies: index.html, super_secret.html, about.html, news_info.html, arcade/index.html.
try {
  var r = document.documentElement;
  var t = localStorage.getItem("retro8080.theme") || "subtle";
  var m = localStorage.getItem("retro8080.mode") || "light";
  if (t != "win" && t != "winxp" && t != "web94" && t != "subtle" && t != "aurora") t = "subtle";
  if (m != "light" && m != "dark" && m != "system") m = "light";
  var dark = m == "dark" || (m == "system" && matchMedia("(prefers-color-scheme: dark)").matches);
  r.dataset.theme = t;
  if (dark) r.dataset.mode = "dark";
  else delete r.dataset.mode;
} catch (e) {}
