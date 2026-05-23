// Copy code button for <pre> blocks
document.addEventListener('DOMContentLoaded', function() {
  document.querySelectorAll('pre').forEach(pre => {
    const btn = document.createElement('button');
    btn.className = 'copy-btn';
    btn.textContent = 'Copy';
    btn.setAttribute('aria-label', 'Copy code to clipboard');
    pre.appendChild(btn);

    btn.addEventListener('click', async () => {
      const code = pre.querySelector('code');
      if (!code) return;

      try {
        await navigator.clipboard.writeText(code.textContent);
        btn.textContent = 'Copied!';
        btn.classList.remove('failed');
        btn.classList.add('copied');
        setTimeout(() => {
          btn.textContent = 'Copy';
          btn.classList.remove('copied');
        }, 2000);
      } catch (err) {
        btn.textContent = 'Failed';
        btn.classList.remove('copied');
        btn.classList.add('failed');
        setTimeout(() => {
          btn.textContent = 'Copy';
          btn.classList.remove('failed');
        }, 2000);
      }
    });
  });
});
