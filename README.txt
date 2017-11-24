	- Scaricare lo script 'build-ffmpeg.sh' da: https://github.com/kewlbear/FFmpeg-iOS-build-script e poi eseguirlo all'interno di una cartella, es: FFmpegWrapper

	- All'interno della cartella FFmpegWrapper, verrano create altre sottocartelle:
		ffmpeg-3.3 (codice sorgente)
		scratch (librerie compilate per le varie piattaforme)
		thin (librerie e header)

	- Copiare all'interno della cartella del progetto, il contenuto di 'thin/arm64' e rinominarla in 'ffmpeg'

	- All'interno del progetto Swift dove usare la libreria FFmpeg:
		TARGETS --> Build Phases --> Link Binary with Libraries:
			- aggiungere tutte le librerie che si trovano sotto la cartella 'thin': x86_64/lib/*.a
			- aggiungere: libz.tbd, libiconv.tbd, libbz2.tbd
			- aggiungere: CoreMedia.framework, AudioToolbox.framework, VideoToolbox.framework
		TARGETS --> Build Settings --> Search Paths:
			- Header Search Paths: "$(SRCROOT)/ffmpeg/include" (non recursive)
			- Library Search Paths: "$(SRCROOT)/ffmpeg/lib"   (non recursive)
	
	- Creare il file 'ffmpeg.map' il cui contenuto dovrà essere:
				module ffmpeg[extern_c] {
				    header "libavformat/avformat.h"
				    header "libavcodec/avcodec.h"
				    header "libavcodec/avcodec.h"
    
				    export *
				}
	
	- TARGETS --> Build Phases --> + New Run Script Phases
		rinominarlo in "Copy Module Maps"
		sportarlo in seconda posizione 
		sotto la TextArea: "Shell /bin/sh" scrivere:
			cp $SRCROOT/ffmpeg.map $SRCROOT/ffmpeg/include/module.map
			
	- Creare i file "ffmpeg.h" e "ffmpeg.c" dove scrivere tutte le funzioni C che sfruttano le librerie di FFmpeg