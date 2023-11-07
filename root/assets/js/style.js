$(document).one('mousedown', function () {
    alert("666");
    if ($('.log_err')[0].clientWidth) {
        $('.log_err').panel('close');
    }
});

1
2
3
4
5