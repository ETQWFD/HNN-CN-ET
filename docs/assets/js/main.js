(function () {
  var REPO = 'ETQWFD/HNN-CN-ET';
  var API_LATEST = 'https://api.github.com/repos/' + REPO + '/releases/latest';
  var API_ALL = 'https://api.github.com/repos/' + REPO + '/releases?per_page=100';
  var ANNOUNCE_URL = 'announcement.json';

  function $(id) { return document.getElementById(id); }

  var toggle = $('navToggle'), links = $('navLinks');
  if (toggle) toggle.addEventListener('click', function () {
    if (links) links.classList.toggle('open');
  });

  function fmtSize(bytes) {
    if (!bytes && bytes !== 0) return '';
    var units = ['B', 'KB', 'MB', 'GB'];
    var i = 0;
    while (bytes >= 1024 && i < units.length - 1) { bytes /= 1024; i++; }
    return bytes.toFixed(bytes >= 100 || i === 0 ? 0 : 1) + ' ' + units[i];
  }

  function fmtDate(iso) {
    if (!iso) return '';
    var d = new Date(iso);
    if (isNaN(d.getTime())) return '';
    return d.getFullYear() + '-' +
      String(d.getMonth() + 1).padStart(2, '0') + '-' +
      String(d.getDate()).padStart(2, '0');
  }

  function pickApk(assets) {
    if (!assets || !assets.length) return null;
    for (var i = 0; i < assets.length; i++) {
      var a = assets[i];
      if (a.name && /\.apk$/i.test(a.name)) return a;
    }
    return null;
  }

  fetch(API_LATEST, { headers: { 'Accept': 'application/vnd.github+json' } })
    .then(function (r) { return r.json(); })
    .then(function (rel) {
      var ver = rel.tag_name || rel.name || '';
      var apk = pickApk(rel.assets);
      if (ver) {
        if ($('heroVer')) $('heroVer').textContent = ver;
        if ($('dlVer')) $('dlVer').textContent = ver;
      }
      if ($('dlTime')) $('dlTime').textContent = '发布时间：' + (fmtDate(rel.published_at) || '—');
      if (apk && apk.browser_download_url) {
        if ($('btnApk')) {
          $('btnApk').href = apk.browser_download_url;
          $('btnApk').setAttribute('download', apk.name || '');
        }
        if ($('btnDownload')) $('btnDownload').href = apk.browser_download_url;
        if ($('dlSize')) $('dlSize').textContent = ' · ' + fmtSize(apk.size);
        if ($('dlName')) $('dlName').textContent = apk.name;
        if ($('downloadNote')) {
          $('downloadNote').textContent = '最新版本 ' + ver + ' · ' + (apk.name || 'APK') +
            (apk.size ? ' · ' + fmtSize(apk.size) : '');
        }
      } else if ($('downloadNote')) {
        $('downloadNote').textContent = 'APK 尚未上传，请稍后再看或到 Releases 页面查看。';
      }
    })
    .catch(function () {
      if ($('downloadNote')) {
        $('downloadNote').textContent = '暂时无法获取版本信息，已保留 Releases 页面链接。';
      }
    });

  fetch(API_ALL, { headers: { 'Accept': 'application/vnd.github+json' } })
    .then(function (r) { return r.json(); })
    .then(function (list) {
      if (!Array.isArray(list)) return;
      var total = 0;
      list.forEach(function (rel) {
        (rel.assets || []).forEach(function (a) { total += (a.download_count || 0); });
      });
      if ($('dlTotal') && total > 0) $('dlTotal').textContent = total.toLocaleString();
    })
    .catch(function () {});

  fetch(ANNOUNCE_URL, { headers: { 'Accept': 'application/json' } })
    .then(function (r) { return r.ok ? r.json() : null; })
    .then(function (a) {
      if (!a || !a.content) return;
      var bar = $('announceBar');
      if (!bar) return;
      if (a.tag) $('announceTag').textContent = a.tag;
      $('announceText').textContent = a.title + '：' + a.content;
      bar.style.display = 'block';
    })
    .catch(function () {});
})();
