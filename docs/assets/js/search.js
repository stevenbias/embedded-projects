// Search functionality for Embedded Mastery documentation
(function() {
  var input = document.getElementById('search-input');
  var resultsDiv = document.getElementById('search-results');
  if (!input || !resultsDiv) return;

  var index = window.SEARCH_INDEX || [];
  var debounceTimer = null;

  // Keyboard shortcut: Ctrl+K or / to focus search
  document.addEventListener('keydown', function(e) {
    if ((e.ctrlKey && e.key === 'k') || (e.key === '/' && document.activeElement !== input)) {
      e.preventDefault();
      input.focus();
      input.select();
    }
    if (e.key === 'Escape') {
      input.blur();
      resultsDiv.style.display = 'none';
    }
  });

  function highlightText(text, query) {
    if (!query) return text;
    var escaped = query.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    var regex = new RegExp('(' + escaped + ')', 'gi');
    return text.replace(regex, '<mark>$1</mark>');
  }

  function renderResults(results, query) {
    if (results.length === 0) {
      resultsDiv.innerHTML = '<div class="search-no-results">No results found for "' + query + '"</div>';
      resultsDiv.style.display = 'block';
      return;
    }

    var html = '';
    results.forEach(function(r) {
      html += '<a href="' + r.f + '.html' + (r.i ? '#' + r.i : '') + '" class="search-result-item">';
      html += '<div class="search-result-page">' + r.p + '</div>';
      if (r.h) {
        html += '<div class="search-result-title">' + highlightText(r.h, query) + '</div>';
      }
      if (r.e) {
        html += '<div class="search-result-excerpt">' + highlightText(r.e, query) + '</div>';
      }
      html += '</a>';
    });

    resultsDiv.innerHTML = html;
    resultsDiv.style.display = 'block';
  }

  function doSearch(query) {
    if (!query || query.length < 2) {
      resultsDiv.style.display = 'none';
      return;
    }

    if (index.length === 0) {
      resultsDiv.innerHTML = '<div class="search-no-results">Search index not loaded. Run "make pages" to rebuild.</div>';
      resultsDiv.style.display = 'block';
      return;
    }

    var q = query.toLowerCase();
    var results = [];

    index.forEach(function(entry) {
      var textLower = (entry.e || '').toLowerCase();
      var headingLower = (entry.h || '').toLowerCase();
      var pageLower = (entry.p || '').toLowerCase();

      var score = 0;
      if (headingLower.indexOf(q) !== -1) score += 100;
      if (pageLower.indexOf(q) !== -1) score += 50;
      if (textLower.indexOf(q) !== -1) score += 10;

      var words = q.split(/\s+/);
      words.forEach(function(w) {
        if (headingLower.indexOf(w) !== -1) score += 20;
        if (textLower.indexOf(w) !== -1) score += 5;
      });

      if (score > 0) {
        entry.score = score;
        results.push(entry);
      }
    });

    results.sort(function(a, b) { return b.score - a.score; });
    renderResults(results.slice(0, 20), query);
  }

  input.addEventListener('input', function() {
    clearTimeout(debounceTimer);
    debounceTimer = setTimeout(function() { doSearch(input.value.trim()); }, 150);
  });

  input.addEventListener('focus', function() {
    if (this.value.trim().length >= 2) {
      doSearch(this.value.trim());
    }
  });

  document.addEventListener('click', function(e) {
    if (!resultsDiv.contains(e.target) && e.target !== input) {
      resultsDiv.style.display = 'none';
    }
  });
})();
