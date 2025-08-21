function updateOperatorName() {
    fetch('/operator.xml')
        .then(response => response.text())
        .then(data => {
            const parser = new DOMParser();
            const xmlDoc = parser.parseFromString(data, "text/xml");
            const operatorName = xmlDoc.getElementsByTagName("name")[0].textContent;
            const ussdHeader = document.getElementById("ussd-header");
            if (ussdHeader) {
                ussdHeader.textContent = "USSD for " + operatorName;
            }
        })
        .catch(error => {
            console.log('Error fetching operator name:', error);
        });
}

// Обновляем имя оператора каждые 5 секунд
setInterval(updateOperatorName, 5000);

// Обновляем сразу при загрузке страницы
window.onload = function() {
    updateOperatorName();
};
