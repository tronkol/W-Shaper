# W-Shaper  v2.0

Plugin di CONTROLUNA per macOS (AU, VST3, app standalone). Fa suonare la tua voce col timbro di un'altra voce, che gli dai come a cappella. Cambia solo il timbro: intonazione, tempi e livello restano i tuoi.

Interfaccia della famiglia Smoker: piatta, verticale (422×640, ridimensionabile), skin SMOKE (default), ABYSS, MOON e PAPER. Il testo dell'interfaccia è in inglese.

## Come funziona

1. **Analisi dell'a cappella.** Al caricamento W-Shaper la divide in istanti, fino a circa 8000. Per ognuno salva due cose:
   - il timbro: la forma dello spettro, staccata dall'intonazione (cepstro, 80 coefficienti);
   - l'impronta di cosa si sta cantando: 12 coefficienti MFCC più l'intensità. L'impronta è normalizzata sulla voce dell'a cappella.

   I silenzi vengono scartati. W-Shaper misura anche fin dove l'a cappella ha davvero suono: sopra quel limite non corregge, così un MP3 non ti taglia le alte.
2. **Mentre canti.** Ogni 5 ms (2,7 in LIVE) calcola l'impronta della tua voce, normalizzata sulla tua voce. All'inizio parte dalle statistiche dell'a cappella e in circa 4 secondi impara le tue. Poi cerca i 6 istanti dell'a cappella più simili a quello che stai cantando e ne fa la media dei timbri. La tua "a" prende il timbro della sua "a", la tua "s" quello della sua "s": la forma del timbro cambia di continuo insieme a te.
3. **Correzione.** Il timbro trovato viene messo alla stessa energia della tua voce nella banda, e la correzione porta il tuo timbro verso quello, banda per banda.

## Pagine

**TIMBRE**
- **Menu delle bande**: LIVE (latenza zero), 513, 1025 (default), 2049, 4097 o 8193 bande.
- **Display**: il tuo timbro (pieno), quello di riferimento (tratteggiato) e la correzione intorno alla linea dello zero. I bordi della banda si trascinano dalle maniglie in basso.
- **START / END** e **SLOPE**: dove trasferire il timbro.
- **AMOUNT**: quanto timbro prendi. Le alzate sono limitate a +24 dB.
- **MAKEUP**: guadagno solo nella banda.
- **ATTACK / RELEASE**: velocità della correzione.
- **DETAIL**: da colore generale a ogni risonanza della voce di riferimento.
- **MATCH**: 0% = sempre il timbro medio dell'a cappella. 100% = per ogni istante, il timbro dell'a cappella che canta la stessa cosa.
- **LOUDNESS**: quanto conta l'intensità nella ricerca. Alto = le tue note forti prendono il timbro delle sue note forti.
- **FORMANT**: sposta il timbro di riferimento di ±12 semitoni, se l'altra voce è molto più acuta o più grave.
- **SMOOTH**: morbidezza dei cambi di timbro.

**REFERENCE**
- **LOAD A CAPPELLA / X**: carica o toglie il file (WAV, AIFF, FLAC, MP3, M4A, CAF; massimo 5 minuti). Il percorso viene salvato nel progetto.
- **Forma d'onda**: mostra gli istanti dell'a cappella che sta usando in quel momento, con una scia, più il numero di istanti analizzati e la qualità della somiglianza.
- **GATE**: quanto si ritira quando canti piano. A 100% nel silenzio non fa niente.
- **BAND LEVEL** e **AMOUNT NOW**.

**Piede**: MIX, OUTPUT, DELTA (senti solo la differenza), meter IN/GR/OUT, stato (impara la tua voce / trasferisce il timbro), latenza.

## Consigli

- **A cappella ideale**: asciutta (senza riverbero né effetti), da 1 a 3 minuti, a banda piena, con tante vocali e parole diverse. Più "vocabolario" contiene, meglio trova l'istante giusto.
- **I primi secondi**: dopo il caricamento canta qualche secondo prima di giudicare. In basso compare "LEARNING YOUR VOICE" finché non ha imparato la tua voce.
- **Nel mix**: usalo al 100% (MIX).

## Verifiche

- **Due voci sintetiche** con tratto vocale diverso cantano le stesse vocali. La distanza dal timbro dell'altra voce, sulla stessa vocale, è questa:
  - prima: 3,7 dB
  - con il solo timbro medio: 2,8 dB
  - con la ricerca dinamica: 0,7 dB (1,9 in LIVE)
- **Livello in uscita**: resta entro ~1 dB da quello in ingresso.
- **Analisi di un a cappella di 3 minuti**: meno di 1 secondo.

## Build

Come gli altri plugin: GitHub Actions (`.github/workflows/build-macos.yml`, copia visibile in `COPIA-workflow-build-macos.yml`). Lo zip in Artifacts contiene `W-Shaper.component`, `W-Shaper.vst3`, `W-Shaper.app` e `Installa.command`.
