// Site only (mkdocs.yml extra_javascript): marks a wide table (data-rsxr-stack) and labels every cell of a wide table with its column header, so the
// stylesheet can show each row as a card on narrow screens. GitHub ignores this file.
document$.subscribe(() => {
  for (const table of document.querySelectorAll(".md-typeset table:not([class])")) {
    const heads = [...table.querySelectorAll("thead th")].map((th) => th.textContent.trim());
    if (heads.length < 4) continue;
    table.dataset.rsxrStack = "";  // an attribute, not a class: the table:not([class]) styles must still apply
    for (const row of table.querySelectorAll("tbody tr")) {
      [...row.children].forEach((cell, i) => cell.setAttribute("data-label", heads[i] || ""));
    }
  }
});
