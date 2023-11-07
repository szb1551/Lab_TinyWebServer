var MPC = document.getElementById("MPC");
var MATH = document.getElementById("MATH");
var CONTROL = document.getElementById("CONTROL");
var OPTIMIZATION = document.getElementById("OPTIMIZATION");
var RL = document.getElementById("RL");


window.onload = function () {
    var oPdf = document.getElementsByClassName("origin_a");
    xhr = new XMLHttpRequest();
    var first = true;
    xhr.open('GET', '../pdf_array.txt', true);
    xhr.send();
    xhr.onreadystatechange = function () {
        if (first && xhr.readyState == 4 && xhr.status == 200) {
            var text = xhr.responseText;
            console.log(text);
            var lines = text.split('\n');
            //console.log(lines);
            //console.log(lines.at(-1));
            for (var i = 0; i < lines.length; i++) {
                var parts = lines[i].split('/');
                var category = parts[6];
                var name = parts.at(-1).split('.')[0];
                var type = parts.at(-1).split('.').at(-1);
                var path = parts.splice(parts.indexOf('Books') + 1).join('/');
                //console.log(name);
                console.log(type);
                // console.log(category);
                // console.log(path);
                var temp = oPdf[0].cloneNode(1);
                temp.className = 'other_a';
                //temp.id = name;
                temp.href = 'data/lxd_data/Books/' + path + '/p';
                temp.getElementsByTagName('p')[0].innerText = (name + '.' + type).substring(0, 25);
                switch (type) {
                    case "pdf":
                        temp.title = name + '.pdf';
                        if (category == 'mpc') {
                            MPC.appendChild(temp);
                        }
                        else if (category == 'math') {
                            MATH.appendChild(temp);
                        }
                        else if (category == 'control') {
                            CONTROL.appendChild(temp);
                        }
                        else if (category == 'optimization') {
                            OPTIMIZATION.appendChild(temp);
                        }
                        else if (category == 'rl') {
                            RL.appendChild(temp);
                        }
                        break;
                    case "zip":
                        temp.title = name + '.zip';
                        temp.download = name + '.zip';
                        temp.getElementsByTagName('img')[0].src = "../img/other/zip.png";
                        if (category == 'mpc') {
                            MPC.appendChild(temp);
                        }
                        else if (category == 'math') {
                            MATH.appendChild(temp);
                        }
                        else if (category == 'control') {
                            CONTROL.appendChild(temp);
                        }
                        else if (category == 'optimization') {
                            OPTIMIZATION.appendChild(temp);
                        }
                        else if (category == 'rl') {
                            RL.appendChild(temp);
                        }
                        break;
                    case "ppt":
                        temp.title = name + '.ppt';
                        temp.download = name + '.ppt';
                        temp.getElementsByTagName('img')[0].src = "../img/other/ppt.png";
                        if (category == 'mpc') {
                            MPC.appendChild(temp);
                        }
                        else if (category == 'math') {
                            MATH.appendChild(temp);
                        }
                        else if (category == 'control') {
                            CONTROL.appendChild(temp);
                        }
                        else if (category == 'optimization') {
                            OPTIMIZATION.appendChild(temp);
                        }
                        else if (category == 'rl') {
                            RL.appendChild(temp);
                        }
                        break;
                    case "html":
                        temp.title = name + '.html';
                        temp.download = name + '.html';
                        temp.getElementsByTagName('img')[0].src = "../img/other/html.png";
                        if (category == 'mpc') {
                            MPC.appendChild(temp);
                        }
                        else if (category == 'math') {
                            MATH.appendChild(temp);
                        }
                        else if (category == 'control') {
                            CONTROL.appendChild(temp);
                        }
                        else if (category == 'optimization') {
                            OPTIMIZATION.appendChild(temp);
                        }
                        else if (category == 'rl') {
                            RL.appendChild(temp);
                        }
                        break;
                    case "rar":
                        temp.title = name + '.rar';
                        temp.download = name + '.rar';
                        temp.getElementsByTagName('img')[0].src = "../img/other/rar.png";
                        if (category == 'mpc') {
                            MPC.appendChild(temp);
                        }
                        else if (category == 'math') {
                            MATH.appendChild(temp);
                        }
                        else if (category == 'control') {
                            CONTROL.appendChild(temp);
                        }
                        else if (category == 'optimization') {
                            OPTIMIZATION.appendChild(temp);
                        }
                        else if (category == 'rl') {
                            RL.appendChild(temp);
                        }
                        break;
                }
            }
            first = false;
        }
    }
};