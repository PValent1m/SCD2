#ifndef WEB_PAGE_H
#define WEB_PAGE_H

#include <Arduino.h>

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <style>
    header {
      background-color: #c1c1c1;
      color: black;
      height: 62px;
      text-align: center;
      margin-top: -20px;
      font-family: sans-serif;
      padding: 10px;
      border-bottom: 3px solid rgba(0, 231, 244, 0.8);
    }
  
    footer {
      background-color:#c1c1c1;
      color: #fff;
      padding: 10px;
      text-align: center;
      position: fixed;
      bottom: 0;
      width: 100%;
    }

    table {
      border-collapse: collapse;
      width: 90%;
      margin-left:5%;
      table-layout: fixed;

    }

   
    thead {
      background-color: rgba(0, 231, 244, 0.9);
    }

   
    .center{
      text-align: center;
      font-family: system-ui;

    }

    tbody tr:hover {
      background-color: rgba(0, 231, 244, 0.9);
    }

 

    .button-container button {
      font-size: 17px;
      margin-left: 5px;
      background-color: white;
      border: none;
      width: 47%;
      height: 45px;
      /* -webkit-transform: skew(-20deg); */
      -moz-transform: skew(-20deg);
      -o-transform: skew(-20deg);
      filter: drop-shadow(0 0 2px rgba(227, 234, 234, 0.6));
      stroke: rgba(184, 202, 203, 0.6);
      transition: background-color 0.1s;
    }
    .tbl-content{
      height:60vh;
      overflow-x:auto;
      margin-top: 0px;
     
      width: 90%;
      margin-left: 5%;
      filter: drop-shadow(0 0 2px rgba(0, 231, 244, 0.8));
      stroke: rgba(0, 231, 244, 0.9);
    }
    th{
      padding: 20px 15px;
      text-align: left;
      font-weight: 500;
      font-size: 20px;
     
      text-transform: uppercase;
    }
    td{
      background: rgba(49, 45, 45,0.3);
      padding: 23px;
      text-align: left;
      vertical-align:middle;
      color: #ffffff;
      font-size: 20px;
      
      border-bottom: solid 1px rgba(255,255,255,0.1);
    }
    body{
      margin: 0px;
      background:black;
    }
    .content_selct{
      width: 90%;
      margin-left: 5%;
      margin-bottom: 20px;
      justify-content: end;
      display: flex;
      border-radius: 0px;
    }
    .button_selct {
      width: 73px;
      display: flex;
      padding: 10px;
      border: none;
      justify-content: center;
      font-size: 20px;
      background: rgba(0, 231, 244, 0.4);
    }
    .button_selcted{
      background: rgba(0, 231, 244, 0.9);
    }
    ::-webkit-scrollbar {
      width: 6px;
    } 
    ::-webkit-scrollbar-track {
      -webkit-box-shadow: inset 0 0 6px rgb(255, 0, 85); 
    } 
    ::-webkit-scrollbar-thumb {
      -webkit-box-shadow: inset 0 0 6px rgb(255, 0, 85); 
    }

    

  </style>
</head>
<body>
  <header>
    <h1>PRD</h1>
  </header>
  <br>
  <div class="content_selct custom-select">
    <select id="sel_sps" class="button_selct" style="width: auto; margin-right: auto;">
      <option value="2000">2000 SPS</option>
      <option value="3750">3750 SPS</option>
      <option value="7500">7500 SPS</option>
      <option value="15000">15000 SPS</option>
      <option value="30000">30000 SPS</option>
    </select>
    <button id="b_sd" class="button_selct button_selcted ">SD</button>
    <button id="b_LittleFS" class="button_selct" disabled>LittleFS</button>

  </div>
  
  <div class="tbl-content">
    <table id="fileTable" style="width: 100%;margin-left:0px;" cellpadding="0" cellspacing="0" border="0">
      <thead>
        <tr>
          <th class="center">Name/Type (Clique para Download)</th>
          <th class="center" style="width: 100px">Size</th>
          <th class="center" style="width: 50px">Type</th>
          <th class="center" style="width: 150px;">Action</th>
        </tr>
      </thead>
      <tbody id="content_tabel">
    </table>
  </div>

  <script>
    var gateway = `ws://${window.location.hostname}/ws`;
    var websocket;

    // Função executada quando a página carrega
    window.addEventListener('load', () => {
      initWebSocket();
      // Solicita a lista de arquivos do LittleFS ao carregar
      document.getElementById("b_sd").click(); 
    });

    function initWebSocket() {
      console.log('Trying to open a WebSocket connection...');
      websocket = new WebSocket(gateway);
      websocket.onopen = onOpen;
      websocket.onclose = onClose;
      websocket.onmessage = onMessage;
    }

    function onOpen(event) {
      console.log('Connection opened');
      // Pergunta ao ESP32 qual a taxa de amostragem atual
      websocket.send("GET_SPS");
    }

    function onClose(event) {
      console.log('Connection closed');
      setTimeout(initWebSocket, 2000);
    }

    // Função que processa as mensagens (lista de arquivos) vindas do ESP32
    function onMessage(event) {
      // Converte o texto JSON em um objeto
      var file = JSON.parse(event.data);
      // Se a mensagem for a taxa de amostragem, apenas atualiza o seletor
      if (file.sps !== undefined) {
        document.getElementById("sel_sps").value = file.sps;
        return;
      }
      // Adiciona o arquivo na tabela da página
      addFileToTable(file.name, file.size, file.type, file.loc);
    }

    // Função para adicionar uma linha na tabela de arquivos
    function addFileToTable(name, size, type, loc) {
      var tbody = document.getElementById('content_tabel');
      var newRow = document.createElement('tr');
      newRow.id = name; // Atribui um ID à linha para poder deletá-la

      // Célula do Nome (agora é um link de download)
      var nameCell = newRow.insertCell(0);
      nameCell.classList.add("center");
      var link = document.createElement('a');
      link.href = `/download?file=${encodeURIComponent(name)}&loc=${loc}`;
      link.textContent = name;
      link.style.color = "white";
      nameCell.appendChild(link);

      // Célula do Tamanho
      var sizeCell = newRow.insertCell(1);
      sizeCell.textContent = size;
      sizeCell.classList.add("center");
      sizeCell.style.width = "100px";
      
      // Célula do Tipo
      var typeCell = newRow.insertCell(2);
      typeCell.textContent = type;
      typeCell.classList.add("center");
      typeCell.style.width = "50px";

      // Célula dos Botões de Ação
      var actionsCell = newRow.insertCell(3);
      actionsCell.classList.add("button-container", "center");
      actionsCell.style.width = "300px";
      
      var deleteButton = document.createElement('button');
      deleteButton.textContent = "Delete";
      // A função de deleção agora também recebe a localização do arquivo
      deleteButton.setAttribute("onclick", `deleteFile('${name}', '${loc}')`);
      actionsCell.appendChild(deleteButton);

      tbody.appendChild(newRow);
    }

    // Função para deletar um arquivo
    function deleteFile(name, loc) {
      var message = `Delete/${loc}:${name}`;
      console.log("Enviando comando de deleção:", message);
      websocket.send(message);
      document.getElementById(name).remove();
    }
    
    // Limpa todas as linhas da tabela
    function clearTable() {
      document.getElementById('content_tabel').innerHTML = '';
    }

    // Evento de clique para o botão SD
    document.getElementById("b_sd").addEventListener("click", () => {
      document.getElementById("b_sd").classList.add("button_selcted");
      document.getElementById("b_LittleFS").classList.remove("button_selcted");
      clearTable();
      websocket.send("SD");
    });

    // Evento de clique para o botão LittleFS
    document.getElementById("b_LittleFS").addEventListener("click", () => {
      document.getElementById("b_LittleFS").classList.add("button_selcted");
      document.getElementById("b_sd").classList.remove("button_selcted");
      clearTable();
      websocket.send("LittleFS");
    });

    // Evento de troca da taxa de amostragem (envia ex: "SPS:1000" para o ESP32)
    document.getElementById("sel_sps").addEventListener("change", () => {
      var valor = document.getElementById("sel_sps").value;
      websocket.send("SPS:" + valor);
    });
    
  </script>
</body>
</html>
)rawliteral";

#endif