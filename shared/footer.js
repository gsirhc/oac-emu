/* Injects the shared site footer. a.pb-close[href] starting with ../ marks a
   machine page (or arcade hub), so Help's target is one level up. */
(function () {
  // the 486 catalog token; with it Help opens about.html, without it news_info.html
  var FULL_TOKEN = "c4e8a1f07b2d49e6a3c58f1d6e90b274";

  function helpHref() {
    var page = new URLSearchParams(location.search).get("full") === FULL_TOKEN ? "about.html" : "news_info.html";
    var close = document.querySelector("a.pb-close");
    var href = close ? close.getAttribute("href") || "" : "";
    return (href.indexOf("..") === 0 ? "../" : "") + page;
  }

  var SOURCE_HREF = "https://github.com/gsirhc/oac-emu/tree/main/";

  function mount() {
    if (document.getElementById("siteFooter")) return;
    var footer = document.createElement("footer");
    footer.id = "siteFooter";
    footer.className = "site-footer";
    var help = document.documentElement.hasAttribute("data-no-help") ? "" :
      '<p class="site-footer-about">' +
      '<a class="about-sign" href="' + helpHref() +
      '" title="Help" aria-label="Help">Help</a></p>';
    footer.innerHTML = help +
      "<p>&copy; 2026 oac-emu.dev - featuring Old-Ass Computer " +
      "Emulation you can use for free.</p>" +
      '<p>Source code available on <a class="source-link" href="' +
      SOURCE_HREF + '" target="_blank" rel="noopener noreferrer">GitHub</a></p>';
    var page = document.querySelector(".page");
    (page || document.body).appendChild(footer);
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", mount);
  else mount();
})();
