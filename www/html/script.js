var select = document.getElementById('filePath');
var resultMessage = document.getElementById('resultMessage');

function loadFileOptions() {
    fetch('/my-uploads')
        .then(function (res) { return res.text(); })
        .then(function (html) {
            var doc = new DOMParser().parseFromString(html, 'text/html');
            select.innerHTML = '';
            doc.querySelectorAll('a').forEach(function (a) {
                var name = decodeURIComponent(a.getAttribute('href'))
                    .replace(/^\/?uploads\/?/, '');
                if (name && name !== '.' && name !== '..') {
                    var opt = document.createElement('option');
                    opt.value = name;
                    opt.textContent = name;
                    select.appendChild(opt);
                }
            });
        });
}

 loadFileOptions();

document.getElementById('deleteButton').addEventListener('click', function () {
    var filePath = select.value;
    if (!filePath) return;

    fetch('/uploads/' + encodeURIComponent(filePath), { method: 'DELETE' })
        .then(function (res) {
            resultMessage.textContent = res.ok
                ? 'Deleted "' + filePath + '"'
                : 'Failed (status ' + res.status + ')';
            resultMessage.className = 'result-message' + (res.ok ? ' success' : ' error');
            loadFileOptions();
        });
});



(function () {
    var title = document.getElementById('auth-title');
    var loginForm = document.getElementById('login-form');
    var loggedInBox = document.getElementById('logged-in-box');

    function showLoggedOut() {
        title.textContent = '🔐 Login';
        loginForm.classList.remove('hidden');
        loggedInBox.classList.add('hidden');
    }

    function showLoggedIn(username) {
        title.textContent = 'Welcome, ' + username + '!';
        loginForm.classList.add('hidden');
        loggedInBox.classList.remove('hidden');
    }

    fetch('/session')
        .then(function (res) { return res.text(); })
        .then(function (text) {
            var prefix = 'Logged in as ';
            if (text.indexOf(prefix) !== 0) { showLoggedOut(); return; }
            showLoggedIn(text.slice(prefix.length).split('\n')[0]);
        })
        .catch(showLoggedOut);
}());