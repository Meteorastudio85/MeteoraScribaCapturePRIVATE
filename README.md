# Scriba Capture

Plugin (VST3 / AU / Standalone) che cattura il suono di un hardware da studio con uno sweep sinusoidale esponenziale interno e poi lo riproduce in tempo reale.

Cosa misura in una sola cattura:

- **Risposta all'impulso** (EQ, colore, trasformatori, riverberi a molla/piastra, casse) tramite deconvoluzione dello sweep.
- **Armoniche** 2a e 3a, separate grazie allo sweep esponenziale, riprodotte con un modello di saturazione.
- **Dinamica** (opzionale): curva di guadagno su 17 livelli a 1 kHz e tempi di attacco/rilascio. Se l'hardware non comprime, la dinamica viene disattivata da sola.
- **Latenza** di andata e ritorno della scheda audio, compensata in automatico.

Per i compressori c'e' anche il modo **DYN**, una misura dedicata e piu' precisa della compressione (vedi sotto).

## Nuovo nome: da Scriba Sweep a Scriba Capture

Dalla versione 1.2.0 il plugin si chiama **Scriba Capture**. I codici interni (produttore `Scrb`, plugin `ScSw`), gli ID dei parametri e il formato dei salvataggi sono rimasti identici, quindi:

- i progetti in cui hai usato Scriba Sweep si riaprono con Scriba Capture, con le stesse impostazioni e la cattura caricata;
- i file `.scriba` gia' salvati restano compatibili.

Prima di installare la nuova versione **cancella il vecchio `Scriba Sweep.vst3`** (e su macOS anche `Scriba Sweep.component`), altrimenti la DAW troverebbe due plugin con lo stesso codice. Poi fai rifare la scansione dei plugin alla DAW.

## Lingua (italiano / inglese)

In basso a destra c'e' il selettore **IT | EN**. Al primo avvio il plugin usa la lingua del sistema; la scelta viene salvata e vale per tutte le istanze e per l'app standalone.

## Compilazione

Il progetto si compila con GitHub Actions: carica la cartella su GitHub e il workflow `.github/workflows/build.yml` parte da solo a ogni push (oppure da *Actions → Build Scriba Capture → Run workflow*). Alla fine trovi negli *Artifacts*:

- `ScribaCapture-Windows` → VST3 + app Standalone (.exe)
- `ScribaCapture-macOS` → VST3 + AU + app Standalone (Intel e Apple Silicon)

Installazione VST3 su Windows: copia `Scriba Capture.vst3` in `C:\Program Files\Common Files\VST3`.
Su macOS il plugin non e' firmato: dopo averlo copiato esegui `xattr -cr` sul file.

## Come catturare

### Metodo consigliato: app Standalone
1. Collega un'uscita della scheda audio all'ingresso dell'hardware e l'uscita dell'hardware a un ingresso della scheda.
2. Apri **Scriba Capture** standalone → *Options / Audio settings*: scegli la scheda, attiva l'ingresso del ritorno e l'uscita che va all'hardware. Togli il mute dell'ingresso ("Mute audio input").
3. Abbassa i monitor: lo sweep esce anche su quell'uscita se e' condivisa.
4. Premi la card **SWEEP**, imposta il livello di test (−18 dBFS e' un buon punto di partenza), la durata dello sweep e la coda (0,3 s per EQ e preamp, 3–6 s per riverberi).
5. Premi **AVVIA CATTURA** e non toccare nulla fino alla fine. Controlla che il meter RITORNO non vada in rosso.
6. Premi **SALVA** per avere il file `.scriba`, da caricare poi nel plugin dentro la DAW.

### Dentro una DAW (es. Studio One)
1. Crea una traccia con ingresso = canale dove rientra l'hardware e monitoraggio attivo.
2. Imposta l'uscita della traccia su un canale d'uscita che va all'hardware (non sul master).
3. Inserisci Scriba Capture, scegli **SWEEP** e premi **AVVIA CATTURA**.

In modo SWEEP e NAM, fuori dalla misura, l'uscita del plugin resta muta per evitare anelli di feedback.

## Risposta in frequenza

Dopo ogni cattura SWEEP il grafico RISPOSTA IN FREQUENZA mostra la curva dell'hardware (0 dB = guadagno a 1 kHz). Con il pulsante di download in alto a destra del grafico puoi scaricare:

- **Immagine PNG** (1600 x 900) con grafico, nome della cattura, latenza, armoniche e dinamica;
- **Dati CSV** con frequenza, livello relativo a 1 kHz e livello assoluto (separatore `;` e virgola decimale, si apre direttamente in Excel);
- entrambi insieme.

## Registrazione grezza

Con l'interruttore **REG. GREZZA** attivo, premendo **SALVA** subito dopo una cattura viene creato anche `nome_raw.wav` accanto al file `.scriba`:

- canale 1: il segnale di test inviato all'hardware
- canali 2-3: il ritorno registrato

Il file `.scriba` contiene i tempi esatti di ogni sezione del test, quindi i due file insieme permettono analisi piu' approfondite (armoniche superiori, saturazione a livelli diversi). La registrazione grezza resta disponibile solo finche' il plugin e' aperto: salvala subito dopo la cattura.

## Cattura NAM (Neural Amp Modeler / TONE3000)

Il modo **NAM** usa lo stesso collegamento con l'hardware per preparare i file di addestramento di un modello neurale NAM, ideale per preamp, valvole, pedali e tutto cio' che distorce.

1. Imposta la sessione (o l'app standalone) a **48 kHz**: l'input di addestramento NAM e' gia' integrato nel plugin (input.wav, 3:10, identico bit per bit all'originale).
2. Scegli la card **NAM**. Se vuoi usare un file diverso, premi **INPUT** e scegli "Carica un altro file di input...".
3. Premi **AVVIA CATTURA NAM**: il file viene suonato nell'hardware e il ritorno registrato.
4. Premi **SALVA OUTPUT NAM**: viene creato un WAV mono 24 bit lungo esattamente quanto l'input, senza modifiche (la latenza la calibra il trainer NAM).
5. Carica l'output (con lo stesso input.wav) su TONE3000 o nel trainer NAM per addestrare il modello.

Consiglio: per i riverberi e le code lunghe usa lo sweep, per la saturazione NAM. I player NAM caricano anche una IR dopo il modello, quindi puoi combinare le due catture.

## Cattura DYN (compressori e limiter)

Il modo **DYN** misura a fondo come comprime l'hardware, con un tono a 1 kHz:

- **Curva statica**: 31 gradini da 2 dB, da (livello max − 60) a livello max. Da qui il plugin stima **soglia**, **ratio**, **knee** e guadagno sotto soglia, con un modello soft knee adattato ai punti misurati.
- **Attacco e rilascio**: 3 tone burst che saltano da (max − 40) dB a max − 20, max − 10 e max. Per ogni burst vengono misurati gain reduction, attacco e rilascio (costante di tempo al 63% e tempo al 90%).
- **Latenza** (con un breve chirp di sincronismo) e **rumore di fondo** del ritorno: i gradini troppo vicini al rumore vengono esclusi dal calcolo.

Come si usa:

1. Collega l'hardware come per lo sweep e regola le manopole del compressore nella posizione da catturare (fai una foto delle manopole).
2. Scegli la card **DYN** e imposta il **LIVELLO MAX** (−6 dBFS e' un buon inizio): deve essere abbastanza alto da far comprimere bene l'hardware, senza far clippare il ritorno.
3. Scegli la **DURATA**: *Veloce* (circa 30 s), *Standard* (circa 1 min) o *Lenta* (circa 1:50, per rilasci lunghi oltre il secondo). Il tempo totale viene mostrato accanto.
4. Premi **AVVIA CATTURA DYN** e non toccare nulla fino alla fine.
5. Il grafico mostra la curva (punti = misura, linea = modello, diagonale tratteggiata = nessuna compressione) e sotto la gain reduction nel tempo di attacco e rilascio. I valori compaiono anche nel pannello RIPRODUZIONE.
6. Premi **SALVA**: vengono creati `nome.csv` (tutti i dati), `nome.png` (il report) e, con REG. GREZZA attivo, `nome_raw.wav`. Dal pulsante di download sul grafico puoi scaricare solo PNG o CSV.

Se il ritorno arriva su un solo ingresso viene analizzato quello con piu' segnale. Se compare l'avviso che il rilascio non si completa, rifai la cattura con durata *Lenta*.

Con **APPLICA A PLAY** attivo, se in PLAY c'e' gia' una cattura SWEEP, curva e tempi misurati da DYN sostituiscono quelli del test dinamica veloce dello sweep. Salvando la cattura con SALVA in modo SWEEP/PLAY il risultato resta nel file `.scriba`.

Per i compressori la sequenza consigliata, con le manopole ferme, e': **SWEEP** (colore e risposta in frequenza), **NAM** (saturazione) e **DYN** (compressione). Una serie di catture per ogni impostazione che vuoi riprodurre.

## Riproduzione

Scegli **PLAY** e usa il plugin su qualsiasi traccia:

- **INPUT**: livello verso il modello (0 dB = stesso livello della cattura). Spinge armoniche e compressione.
- **ARMONICHE**: quantita' della saturazione misurata (100% = come l'hardware).
- **DINAMICA**: quantita' della compressione misurata.
- **MIX**: dry/wet, con il dry allineato in fase.
- **OUTPUT**: livello d'uscita.

La cattura viene salvata nella sessione della DAW. **ESPORTA WAV** salva la risposta all'impulso per usarla in altri convolutori; **CARICA** accetta anche WAV/AIFF di altre IR.

## Limiti

- Una cattura fotografa l'hardware con le manopole in una sola posizione.
- La saturazione e' un modello di 2a e 3a armonica: le macchine molto sporche vengono avvicinate, non clonate.
- Per i compressori vengono riprodotti curva statica e tempi: comportamenti program-dependent (es. opto) sono semplificati.
- DYN misura con un tono a 1 kHz: tempi di attacco sotto ~1 ms vengono riportati come circa 1 ms (limite della finestra di misura).
