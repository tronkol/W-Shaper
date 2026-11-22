# W-Shaper  v1.0

Plugin di CONTROLUNA per macOS (AU, VST3, app standalone). È l'algoritmo N-SHAPER di Smoker reso autonomo. Rimodella il timbro della voce verso un riferimento: un rumore oppure un file audio, per esempio un'altra voce. Viene copiata solo la forma dello spettro: il livello resta quello della tua voce.

Interfaccia della famiglia Smoker: piatta, verticale (422×640, ridimensionabile), skin SMOKE (default), ABYSS, MOON e PAPER. Il testo dell'interfaccia è in inglese.

## Pagine

**SHAPER**
- **SIBILANTS / FULL SIGNAL**: lavora solo sulle consonanti trovate dal detector, oppure su tutta la voce (default).
- **Menu delle bande**: LIVE (latenza zero), 513, 1025 (default), 2049, 4097 o 8193 bande.
- **Display**: spettro della voce, timbro di riferimento tratteggiato, correzione intorno alla linea dello zero (su = alzato, giù = tagliato). I due bordi della banda si trascinano.
- **START / END** (slider a due cursori): la banda da rimodellare.
- **SLOPE**: pendenza dei due confini.
- **AMOUNT**: quanto la voce va verso il riferimento. Le alzate sono limitate a +18 dB.
- **MAKEUP**: guadagno solo nella banda.
- **ATTACK / RELEASE**: velocità della correzione. ATTACK quando taglia di più, RELEASE quando torna su.
- **DETAIL**: risoluzione del confronto, da 1 ottava (colore generale) a 1/24 di ottava (dettaglio fine).
- **DYNAMIC**: quanto l'intervento segue il livello della voce. A 0% è uguale ovunque; a 100% cala quando la voce scende e si spegne nel silenzio.
- **FORMANT**: sposta il timbro di riferimento di ±12 semitoni. Serve se la voce di riferimento è più grave o più acuta della tua.
- **BAND LEVEL**: livello della banda e AMOUNT del momento.

**SOURCE**
- **NOISE**: riferimento = il rumore scelto. Sono gli stessi di Smoker: WHITE, PINK, BROWN, BLUE, VIOLET, GREY, BREATH, SILK, SMOKE, TAPE.
- **FILE AVERAGE**: riferimento = il timbro medio di tutto il file (le parti di silenzio sono escluse).
- **FILE FOLLOW**: il file scorre in loop, senza suonare, e la voce prende di continuo il timbro del punto in cui si trova.
  - **RATE**: velocità di scorrimento, dal 10 al 400%.
  - **SMOOTH**: quanto sono morbidi i cambi di timbro.
  - **FREEZE**: blocca il timbro del momento.
  - I silenzi del file vengono saltati: resta l'ultimo timbro valido.
- **LOAD FILE / X**: carica o toglie il file (WAV, AIFF, FLAC, MP3, M4A, CAF; massimo 5 minuti). Il percorso viene salvato nel progetto.
- Finché non c'è un file, W-Shaper usa il rumore scelto.

**DETECT**: classi S Z SH, F V e CH J con SENS, poi HOLD e GUARD, e la storia degli ultimi 3 secondi. Serve in modalità SIBILANTS.

**Piede**: MIX, OUTPUT, DELTA (senti solo la differenza), CONS. ONLY, meter IN/GR/OUT, luci del detector, latenza.

## Note

- **File a banda stretta**: se il file di riferimento è a banda stretta (MP3 a 16 kHz, registrazioni telefoniche), sopra la sua banda il riferimento è vuoto e W-Shaper taglierebbe le alte. In quel caso abbassa END fin dove il file ha contenuto.
- **Latenza**: 64 ms a 1025 bande, compensata dall'host; zero in LIVE.

## Build

Come gli altri plugin: GitHub Actions (`.github/workflows/build-macos.yml`, copia visibile in `COPIA-workflow-build-macos.yml`). Lo zip in Artifacts contiene `W-Shaper.component`, `W-Shaper.vst3`, `W-Shaper.app` e `Installa.command`.
