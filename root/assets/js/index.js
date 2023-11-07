var user = document.getElementsByClassName("user_md");
var root = document.getElementsByClassName("root_md");
window.onload = function () {
    var oVideo = document.getElementsByClassName("video_origin");
    var tar = document.getElementsByClassName("video_list");
    xhr = new XMLHttpRequest();
    var first = true;
    xhr.open('GET', '../video_array.txt', true);
    xhr.send();
    xhr.onreadystatechange = function () {
        if (first && xhr.readyState == 4 && xhr.status == 200) {
            var text = xhr.responseText;
            console.log(text);
            var lines = text.split('\n');
            console.log(lines);
            //console.log(lines.at(-1));
            //var temp = oVideo[0].cloneNode(1);
            for (var i = 0; i < lines.length; i++) {
                var parts = lines[i].split('/');
                var name = parts.at(-1).split('.').at(-2);
                var path = parts.splice(parts.indexOf('lxd_data') + 1).join('/');
                console.log(path);
                var temp = oVideo[0].cloneNode(1);
                temp.className = "video_simple";
                temp.getElementsByTagName("a")[0].id = name;
                temp.getElementsByTagName("a")[0].name = path;
                temp.getElementsByTagName("img")[0].src = '../img/video/pic01' + '.jpg';
                temp.getElementsByTagName("span")[0].innerText = name;
                tar[0].appendChild(temp);
            }
            first = false;
        }
    }
};

function ChangeWord() {
    var elements = document.getElementsByClassName("vidioTitle clearfix");
    for (var i = 0; i < elements.length; i++) {
        elements[i].innerText = '我好了';
    }

}

function myClick(id) {
    var u = document.getElementById(id)
    window.open(u.href + u.name);
};

user.onclick = function () {
    alert('566');
}