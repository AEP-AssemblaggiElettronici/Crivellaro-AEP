const char page[] PROGMEM = R"___(
<html>
  <!-- foglio di stile della pagina HTML dello webserver -->
  <style>
    #box{
        display: flex;
        flex-diretcion: row;
        alilgn-items: center;
        justify-content: center;
        align-content: center;
        align-self: center;
        padding: 10%;
    }
    #card{
        display: flex;
        justify-content: center;
        align-content: center;
        align-self: center;
        max-width: 600px;
        min-height: 250px;
        background: #333333;
        padding: 30px;
        color: #FFF;
        font-size: 30px;
        margin:20px;
        box-shadow: 0px 8px 22px -8px rgba(0,0,0,0.50);
        border-radius: 10%;
    }
    input{
      font-size: 130%;
    }
  </style>

  <!-- ritorniamo il valore del ritardo tramite questa promise JS che lo scrive nello span dell'HTML,
       ogni secondo il valore viene refreshato in modo che se viene cambiato da diversi dispositivi
       rimane sempre aggiornato -->
  <script>
    setInterval(()=>{
      fetch("/prendiTempoDelay").then(r => {
        return r.json();
      }).then(rr => {
        document.getElementById("tempoDelay").innerHTML = rr;
      });
    }, 1000);
  </script>

  <!-- codice HTML del webserver -->
  <body>
    <div id="box">
      <div id="card">
        <form align="center" action="/invia">
          <p>Inserire tempo di delay 
          <br>(in secondi, valore massimo 60)</p> 
          <p>Lasciare vuoto per impostare a 0</p>
          <p>Attuale ritardo: <span id="tempoDelay"</span></p>
          <input name="ritardoPagina" type="number" min="0" max="60"/>
          <input type="submit" value="OK"/>
        </form>
      </div>
    </div>
  </body>
</html>
)___";