# Gerenciador de Mídia — ZephyrOS v0.1

## Resumo de Progresso

| Fase | Total | Feito | Parcial | Restante |
|------|-------|-------|---------|----------|
| 1. Infraestrutura de áudio | 38 | 28 | 1 | 9 |
| 2. Decodificadores de áudio | 52 | 0 | 0 | 52 |
| 3. Reprodutor de vídeo | 48 | 0 | 0 | 48 |
| 4. Manipulação de imagens | 44 | 12 | 0 | 32 |
| 5. Gerenciador de mídia (biblioteca) | 56 | 0 | 0 | 56 |
| 6. Interface TUI do Player | 64 | 6 | 0 | 58 |
| 7. Configurações e equalização | 36 | 0 | 0 | 36 |
| **TOTAL** | **338** | **46** | **1** | **291** |

**Progresso geral: 14%** (46/338 itens completos, 1 parcial)

---

## Atalhos de Teclado

| Atalho | Ação |
|--------|------|
| F11 | Abrir/fechar Media Player |
| Space | Play/Pause |
| S | Stop |
| M | Mute/Unmute |
| +/- | Volume Up/Down |
| ←/→ | Retroceder/Avançar 5s |
| Shift+←/Shift+→ | Retroceder/Avançar 30s |
| Esc | Fechar janela |
| Tab | Alternar entre seções |
| Enter | Selecionar arquivo |
| Delete | Remover da playlist |
| N | Próxima faixa |
| P | Faixa anterior |

---

## Fase 1: Infraestrutura de Áudio ✅

### 1.1 Driver AC97

- [x] PCI scan para class 0x04 subclass 0x01 — `ac97.c:77-124`
- [x] Leitura de BAR0/BAR1 — `ac97.c:88-95`
- [x] Bus mastering enable — `ac97.c:96`
- [x] Reset do codec — `ac97.c:55-59`
- [x] Configuração de volume (master + PCM) — `ac97.c:100-105`
- [x] Configuração de sample rate (44100 Hz) — `ac97.c:106-109`
- [x] DMA buffer allocation (4096 bytes) — `ac97.c:130-140`
- [x] Play PCM via DMA — `ac97.c:126-154`
- [x] Stop playback — `ac97.c:156-170`
- [x] Volume control (0-31) — `ac97.c:172-179`
- [x] IRQ handler — `ac97.c:185-197`
- [ ] Criar função `ac97_get_position()` para posição atual no buffer
- [ ] Criar suporte a múltiplos sample rates (8000, 11025, 22050, 44100, 48000)
- [ ] Criar suporte a 8-bit e 16-bit PCM
- [ ] Criar suporte a mono e stereo

### 1.2 PC Speaker

- [x] Inicialização — `speaker.c:8-10`
- [x] Beep (frequência + duração) — `speaker.c:12-36`
- [x] Melodia — `speaker.c:43-55`
- [x] Desligar — `speaker.c:38-41`
- [ ] Criar função `speaker_set_volume(level)` para volume (duty cycle)

### 1.3 WAV Player

- [x] Parser de WAV completo (RIFF, fmt, data chunks) — `wav.c:41-88`
- [x] Suporte a PCM (format 1) — `wav.c:55-65`
- [x] Suporte a 8-bit e 16-bit — `wav.c:60-62`
- [x] Suporte a mono e stereo — `wav.c:57`
- [x] Cálculo de duração — `wav.c:106-108`
- [x] Integração com AC97 para playback — `wav.c:90-94`
- [ ] Criar suporte a WAV com compressão (ADPCM, etc.)
- [ ] Criar suporte a WAV com metadata (INFO chunk)

### 1.4 Media Player App

- [x] Play de áudio WAV — `mediaplayer.c:72-95`
- [x] Stop — `mediaplayer.c:198-205`
- [x] Pause — `mediaplayer.c:207-212`
- [x] Resume — `mediaplayer.c:214-220`
- [x] Status tracking (position, duration) — `mediaplayer.c:222-239`
- [x] Comandos shell: play, view, stop — `shell.c:481-508`
- [ ] Criar suporte a next/previous track
- [ ] Criar suporte a repeat (one/all/off)
- [ ] Criar suporte a shuffle

### 1.5 Sound Settings

- [x] Volume control (Mudo/Baixo/Medio/Alto/Maximo) — `settings.c:107-118`
- [x] Beep ao iniciar (toggle) — `settings.c:110`
- [x] Som teclado (toggle) — `settings.c:112`
- [ ] Criar equalizador básico (bass, mid, treble)
- [ ] Criar balance (left/right)
- [ ] Criar configurações de output (stereo/mono)

---

## Fase 2: Decodificadores de Áudio ⬜

### 2.1 Decoder MP3

- [ ] Criar módulo `src/media/mp3.c` e `mp3.h`
- [ ] Implementar parser de frame MP3:
  - [ ] Detectar sync word (0xFFE/0xFFF)
  - [ ] Ler header (version, layer, bitrate, sample rate, channel mode)
  - [ ] Ler side information
  - [ ] Ler frame data
- [ ] Implementar decodificação básica:
  - [ ] Scale factor band decoding
  - [ ] Huffman decoding
  - [ ] Inverse MDCT
  - [ ] Polyphase synthesis
  - [ ] De-emphasis
- [ ] Criar struct `mp3_frame_t`:
  ```
  - version         = uint8_t (2.5, 2, 1, 0)
  - layer           = uint8_t (I, II, III)
  - bitrate         = uint16_t (kbps)
  - sample_rate     = uint32_t (Hz)
  - channel_mode    = enum (STEREO, JOINT_STEREO, DUAL, MONO)
  - data[]          = uint8_t
  - data_size       = uint32_t
  ```
- [ ] Criar função `mp3_open(filename, decoder)` para abrir arquivo
- [ ] Criar função `mp3_read_frame(decoder, frame)` para ler frame
- [ ] Criar função `mp3_decode_frame(frame, pcm_buffer)` para decodificar
- [ ] Criar função `mp3_close(decoder)` para fechar
- [ ] Integrar com AC97 para playback
- [ ] Criar suporte a ID3v1 tags (título, artista, álbum, ano)
- [ ] Criar suporte a ID3v2 tags (mais completo)

### 2.2 Decoder OGG/Vorbis

- [ ] Criar módulo `src/media/ogg.c` e `ogg.h`
- [ ] Implementar parser de container OGG:
  - [ ] Ler page header (capture pattern, version, type, granule)
  - [ ] Ler segments
- [ ] Implementar decoder Vorbis básico:
  - [ ] Book codebook
  - [ ] Floor curves
  - [ ] Residue vectors
  - [ ] Mapping
  - [ ] Mode dispatch
- [ ] Criar struct `ogg_decoder_t`
- [ ] Criar função `ogg_open(filename, decoder)`
- [ ] Criar função `ogg_read_page(decoder, page)`
- [ ] Criar função `ogg_decode_audio(decoder, pcm_buffer)`
- [ ] Criar função `ogg_close(decoder)`
- [ ] Integrar com AC97

### 2.3 Decoder FLAC

- [ ] Criar módulo `src/media/flac.c` e `flac.h`
- [ ] Implementar parser FLAC:
  - [ ] Ler metadata block (stream info, vorbis comment, etc.)
  - [ ] Ler frame header
  - [ ] Ler subframes
- [ ] Implementar decoder FLAC básico:
  - [ ] Constant subframe
  - [ ] Verbatim subframe
  - [ ] Fixed subframe
  - [ ] LPC subframe
  - [ ] Rice coding
- [ ] Criar struct `flac_decoder_t`
- [ ] Criar função `flac_open(filename, decoder)`
- [ ] Criar função `flac_read_frame(decoder, frame)`
- [ ] Criar função `flac_decode_frame(frame, pcm_buffer)`
- [ ] Criar função `flac_close(decoder)`
- [ ] Integrar com AC97

### 2.4 Mixer de Áudio

- [ ] Criar módulo `src/media/mixer.c` e `mixer.h`
- [ ] Criar struct `mixer_channel_t`:
  ```
  - id              = uint8_t
  - volume          = uint8_t (0-255)
  - pan             = int8_t (-128 a 127)
  - sample_rate     = uint32_t
  - bits_per_sample = uint8_t
  - channels        = uint8_t
  - data[]          = int16_t* (PCM buffer)
  - data_size       = uint32_t
  - position        = uint32_t
  - playing         = bool
  - looping         = bool
  ```
- [ ] Criar mixer com 8 canais simultâneos
- [ ] Criar função `mixer_add_channel(config)` para adicionar canal
- [ ] Criar função `mixer_remove_channel(id)` para remover canal
- [ ] Criar função `mixer_set_volume(id, volume)` por canal
- [ ] Criar função `mixer_set_pan(id, pan)` por canal
- [ ] Criar função `mixer_mix(output_buffer, samples)` para mixar
- [ ] Implementar mixagem:
  - [ ] Som de samples (com clamp para evitar overflow)
  - [ ] Aplicar volume por canal
  - [ ] Aplicar pan (balance L/R)
  - [ ] Interpolação se sample rates diferem
- [ ] Integrar com AC97 (chamar mixer_mix a cada DMA buffer fill)

### 2.5 Playlist

- [ ] Criar módulo `src/media/playlist.c` e `playlist.h`
- [ ] Criar struct `playlist_t`:
  ```
  - name[32]        = "Minha Playlist"
  - tracks[MAX_TRACKS] = playlist_track_t
  - track_count     = uint32_t
  - current_index   = uint32_t
  - shuffle         = bool
  - repeat_mode     = enum (OFF, ONE, ALL)
  ```
- [ ] Criar struct `playlist_track_t`:
  ```
  - filename[64]    = "music/song.mp3"
  - title[64]       = "Nome da Música"
  - artist[64]      = "Nome do Artista"
  - album[64]       = "Nome do Álbum"
  - duration_ms     = uint32_t
  - format          = enum (WAV, MP3, OGG, FLAC)
  ```
- [ ] Criar função `playlist_create(name)` para criar playlist
- [ ] Criar função `playlist_add(playlist, track)` para adicionar faixa
- [ ] Criar função `playlist_remove(playlist, index)` para remover faixa
- [ ] Criar função `playlist_clear(playlist)` para limpar
- [ ] Criar função `playlist_next(playlist)` para próxima faixa
- [ ] Criar função `playlist_prev(playlist)` para faixa anterior
- [ ] Criar função `playlist_shuffle(playlist)` para embaralhar
- [ ] Criar função `playlist_save(playlist, filename)` para salvar em arquivo
- [ ] Criar função `playlist_load(filename)` para carregar de arquivo
- [ ] Formato de playlist (.m3u simples):
  ```
  #EXTM3U
  #EXTINF:180,Nome da Música
  music/song.mp3
  #EXTINF:240,Outra Música
  music/other.ogg
  ```

---

## Fase 3: Reprodutor de Vídeo ⬜

### 3.1 Decodificador de Vídeo Básico

- [ ] Criar módulo `src/media/video.c` e `video.h`
- [ ] Implementar decodificador RAW:
  - [ ] BMP sequencial (slideshow)
  - [ ] PPM (Portable Pixmap) frame a frame
  - [ ] YUV raw (se suportado)
- [ ] Criar struct `video_frame_t`:
  ```
  - width           = uint16_t
  - height          = uint16_t
  - data[]          = uint8_t* (pixels)
  - format          = enum (RGB888, RGB565, YUV420)
  - timestamp       = uint32_t (ms)
  - duration        = uint32_t (ms)
  ```
- [ ] Criar struct `video_decoder_t`:
  ```
  - file            = FILE*
  - width           = uint16_t
  - height          = uint16_t
  - fps             = uint8_t
  - total_frames    = uint32_t
  - current_frame   = uint32_t
  - audio_track     = uint8_t
  - frames[]        = video_frame_t*
  ```
- [ ] Criar função `video_open(filename, decoder)` para abrir
- [ ] Criar função `video_read_frame(decoder, frame)` para ler frame
- [ ] Criar função `video_render_frame(frame)` para renderizar no VESA
- [ ] Criar função `video_close(decoder)` para fechar

### 3.2 Container AVI

- [ ] Criar módulo `src/media/avi.c` e `avi.h`
- [ ] Implementar parser AVI:
  - [ ] Ler RIFF header (AVI )
  - [ ] Ler LIST hdrl (header)
  - [ ] Ler avih (main header: width, height, fps, streams)
  - [ ] Ler strh (stream header: type, codec, scale, rate)
  - [ ] Ler strf (stream format: BITMAPINFOHEADER or WAVEFORMATEX)
  - [ ] Ler LIST movi (data)
- [ ] Criar struct `avi_file_t`:
  ```
  - width           = uint32_t
  - height          = uint32_t
  - fps             = uint32_t
  - total_frames    = uint32_t
  - video_codec     = uint32_t
  - audio_codec     = uint32_t
  - video_stream    = FILE*
  - audio_stream    = FILE*
  - frame_index[]   = uint32_t* (seeking)
  ```
- [ ] Criar função `avi_open(filename, avi)` para abrir
- [ ] Criar função `avi_read_video_frame(avi, frame)` para ler frame
- [ ] Criar função `avi_read_audio_chunk(avi, buffer)` para ler áudio
- [ ] Criar função `avi_seek(avi, frame_number)` para busca
- [ ] Criar função `avi_close(avi)` para fechar
- [ ] Suportar codecs: RAW, RLE8, RLE4, MJPEG (básico)

### 3.3 Player de Vídeo

- [ ] Criar módulo `src/media/videoplayer.c` e `videoplayer.h`
- [ ] Criar struct `video_player_t`:
  ```
  - decoder         = video_decoder_t*
  - state           = enum (STOPPED, PLAYING, PAUSED)
  - position_ms     = uint32_t
  - duration_ms     = uint32_t
  - volume          = uint8_t
  - fullscreen      = bool
  - speed           = uint8_t (50%, 100%, 150%, 200%)
  ```
- [ ] Criar função `videoplayer_open(filename)` para abrir
- [ ] Criar função `videoplayer_play()` para iniciar
- [ ] Criar função `videoplayer_pause()` para pausar
- [ ] Criar função `videoplayer_stop()` para parar
- [ ] Criar função `videoplayer_seek(ms)` para busca
- [ ] Criar função `videoplayer_set_speed(speed)` para velocidade
- [ ] Criar loop de renderização:
  - [ ] Ler próximo frame
  - [ ] Calcular timing (baseado no timer)
  - [ ] Renderizar frame no VESA
  - [ ] Processar áudio correspondente
  - [ ] Aguardar próximo frame (baseado no FPS)
- [ ] Criar suporte a fullscreen (mudar resolução VESA)
- [ ] Criar suporte a letterboxing (manter proporção)

### 3.4 Formatos de Vídeo

- [ ] Criar suporte a AVI (já implementado acima)
- [ ] Criar suporte a WebM (container simples):
  - [ ] Parser de EBML header
  - [ ] Suporte a VP8/VP9 (decoder básico)
  - [ ] Suporte a Vorbis/Opus (áudio)
- [ ] Criar suporte a MKV (Matroska):
  - [ ] Parser de Segment
  - [ ] Suporte a H.264 (decoder básico)
  - [ ] Suporte a AAC (áudio)
- [ ] Criar suporte a MP4 (MPEG-4):
  - [ ] Parser de atoms (moov, mdat, etc.)
  - [ ] Suporte a H.264
  - [ ] Suporte a AAC
- [ ] Nota: Decoders de vídeo H.264/VP8 são complexos. Para versão educacional, usar apenas codecs simples (RAW, RLE, MJPEG).

---

## Fase 4: Manipulação de Imagens ⬜

### 4.1 BMP Loader (já existe)

- [x] Parser BMP completo (file header, info header) — `bmp.c:35-92`
- [x] Suporte a 1, 4, 8, 24 bpp — `bmp.c:60-70`
- [x] Color table para paleta — `bmp.c:68-75`
- [x] Renderização no VESA — `bmp.c:156-165`
- [x] Escalamento — `bmp.c:167-180`
- [x] Suporte a bottom-up e top-down — `bmp.c:100-105`
- [ ] Criar suporte a BMP 32bpp (com alpha channel)
- [ ] Criar suporte a BMP compressed (RLE8, RLE4)

### 4.2 PNG Loader

- [ ] Criar módulo `src/media/png.c` e `png.h`
- [ ] Implementar parser PNG:
  - [ ] Ler signature (8 bytes)
  - [ ] Ler IHDR (width, height, bit depth, color type, compression, filter)
  - [ ] Ler IDAT (compressed data)
  - [ ] Ler IEND (end marker)
- [ ] Implementar decodificação:
  - [ ] Inflate (decompressão zlib)
  - [ ] Unfilter (None, Sub, Up, Average, Paeth)
  - [ ] Reconstruct pixels
- [ ] Criar struct `png_image_t`:
  ```
  - width           = uint32_t
  - height          = uint32_t
  - bit_depth       = uint8_t
  - color_type      = uint8_t (0=grayscale, 2=RGB, 3=indexed, 4=gray+alpha, 6=RGBA)
  - data[]          = uint8_t* (pixels)
  - palette[]       = uint8_t* (256 cores)
  ```
- [ ] Criar função `png_load(filename, image)` para carregar
- [ ] Criar função `png_draw(image, x, y)` para renderizar
- [ ] Criar função `png_free(image)` para liberar
- [ ] Integrar com VESA para renderização

### 4.3 JPEG Loader

- [ ] Criar módulo `src/media/jpeg.c` e `jpeg.h`
- [ ] Implementar parser JPEG:
  - [ ] Ler SOI marker (0xFFD8)
  - [ ] Ler APP0/APP1 (JFIF/EXIF)
  - [ ] Ler DQT (quantization tables)
  - [ ] Ler SOF0 (frame header: width, height, components)
  - [ ] Ler DHT (Huffman tables)
  - [ ] Ler SOS (start of scan)
  - [ ] Ler scan data
- [ ] Implementar decodificação:
  - [ ] Huffman decoding
  - [ ] Inverse quantization
  - [ ] Inverse DCT
  - [ ] YCbCr to RGB conversion
  - [ ] Unpacking (baseline JPEG)
- [ ] Criar struct `jpeg_image_t`:
  ```
  - width           = uint32_t
  - height          = uint32_t
  - components      = uint8_t (1=grayscale, 3=YCbCr)
  - data[]          = uint8_t* (RGB pixels)
  ```
- [ ] Criar função `jpeg_load(filename, image)` para carregar
- [ ] Criar função `jpeg_draw(image, x, y)` para renderizar
- [ ] Criar função `jpeg_free(image)` para liberar
- [ ] Nota: JPEG decoding é complexo. Para versão educacional, suportar apenas baseline JPEG sem progressive.

### 4.4 GIF Loader

- [ ] Criar módulo `src/media/gif.c` e `gif.h`
- [ ] Implementar parser GIF:
  - [ ] Ler header (GIF87a/GIF89a)
  - [ ] Ler Logical Screen Descriptor
  - [ ] Ler Global Color Table
  - [ ] Ler Image Descriptor
  - [ ] Ler Local Color Table (se presente)
  - [ ] Ler LZW compressed data
  - [ ] Ler Extension Blocks (GCE, COMMENT, etc.)
- [ ] Implementar decodificação:
  - [ ] LZW decompression
  - [ ] Frame rendering
- [ ] Criar suporte a GIF animado:
  - [ ] Ler múltiplos frames
  - [ ] Ler delay entre frames (GCE)
  - [ ] Ler disposal method
- [ ] Criar struct `gif_image_t`:
  ```
  - width           = uint16_t
  - height          = uint16_t
  - frames[]        = gif_frame_t
  - frame_count     = uint32_t
  - loop            = bool
  ```
- [ ] Criar função `gif_load(filename, image)` para carregar
- [ ] Criar função `gif_render_frame(image, frame_index)` para renderizar
- [ ] Criar função `gif_animate(image)` para animar (loop)
- [ ] Criar função `gif_free(image)` para liberar

### 4.5 Image Viewer

- [ ] Criar módulo `src/media/imageviewer.c` e `imageviewer.h`
- [ ] Criar struct `image_viewer_t`:
  ```
  - filename[64]    = "photo.bmp"
  - image           = void* (bmp/png/jpeg/gif)
  - format          = enum (BMP, PNG, JPEG, GIF)
  - width           = uint32_t
  - height          = uint32_t
  - zoom            = uint8_t (25%, 50%, 75%, 100%, 150%, 200%)
  - rotation        = uint16_t (0, 90, 180, 270)
  - pan_x, pan_y    = int32_t
  ```
- [ ] Criar função `imageviewer_open(filename)` para abrir
- [ ] Criar função `imageviewer_render()` para renderizar
- [ ] Criar função `imageviewer_zoom(level)` para zoom
- [ ] Criar função `imageviewer_rotate(degrees)` para girar
- [ ] Criar função `imageviewer_pan(dx, dy)` para mover
- [ ] Criar função `imageviewer_fit_to_screen()` para ajustar
- [ ] Criar suporte a slide show (próxima imagem automaticamente)
- [ ] Criar comando shell `gallery` para abrir image viewer

---

## Fase 5: Gerenciador de Mídia (Biblioteca) ⬜

### 5.1 Scanner de Mídia

- [ ] Criar módulo `src/media/medialib.c` e `medialib.h`
- [ ] Criar struct `media_file_t`:
  ```
  - id              = uint32_t
  - filename[64]    = "music/song.mp3"
  - type            = enum (AUDIO, VIDEO, IMAGE)
  - format          = enum (WAV, MP3, OGG, FLAC, AVI, BMP, PNG, JPEG, GIF)
  - title[64]       = "Nome da Música"
  - artist[64]      = "Nome do Artista"
  - album[64]       = "Nome do Álbum"
  - duration_ms     = uint32_t
  - size_bytes      = uint32_t
  - width           = uint16_t (para imagens/vídeo)
  - height          = uint16_t (para imagens/vídeo)
  - sample_rate     = uint32_t (para áudio)
  - bitrate         = uint32_t (para áudio/vídeo)
  - date_added      = uint32_t (timestamp)
  - last_played     = uint32_t (timestamp)
  - play_count      = uint32_t
  - rating          = uint8_t (0-5 estrelas)
  - favorite        = bool
  ```
- [ ] Criar array `media_library[MAX_MEDIA_FILES]` (máximo 256 arquivos)
- [ ] Criar função `medialib_scan(directory)` para escanear diretório
  - [ ] Listar arquivos com `fs_list_dir()`
  - [ ] Filtrar por extensão (.wav, .mp3, .ogg, .flac, .avi, .bmp, .png, .jpg, .gif)
  - [ ] Ler metadata (ID3 para MP3, headers para BMP/PNG/etc.)
  - [ ] Adicionar à biblioteca
- [ ] Criar função `medialib_add(file)` para adicionar arquivo
- [ ] Criar função `medialib_remove(id)` para remover
- [ ] Criar função `medialib_find(name)` para buscar por nome
- [ ] Criar função `medialib_find_by_type(type)` para buscar por tipo
- [ ] Criar função `medialib_get_recent()` para recentes
- [ ] Criar função `medialib_get_favorites()` para favoritos
- [ ] Criar função `medialib_get_most_played()` para mais tocados
- [ ] Salvar biblioteca em `/media/library.db`

### 5.2 Metadata de Áudio

- [ ] Criar módulo `src/media/metadata.c` e `metadata.h`
- [ ] Implementar leitura de ID3v1:
  - [ ] Ler 128 bytes do final do arquivo
  - [ ] Extrair: título (30), artista (30), álbum (30), ano (4), comentário (30), gênero (1)
- [ ] Implementar leitura de ID3v2:
  - [ ] Ler header (10 bytes)
  - [ ] Ler frames (TIT2, TPE1, TALB, TDRC, TRCK, TPOS, APIC, etc.)
  - [ ] Suportar encoding ISO-8859-1, UTF-16
- [ ] Implementar leitura de Vorbis Comment:
  - [ ] Ler metadata block
  - [ ] Extrair: TITLE, ARTIST, ALBUM, DATE, TRACKNUMBER
- [ ] Implementar leitura de FLAC metadata:
  - [ ] Ler STREAMINFO block
  - [ ] Ler VORBIS_COMMENT block
- [ ] Implementar leitura de MP4 atoms:
  - [ ] Ler moov/udta/meta/ilst
  - [ ] Extrair: ©nam, ©ART, ©alb, ©day

### 5.3 Biblioteca de Mídia

- [ ] Criar funções de organização:
  - [ ] `medialib_sort_by_name()` — ordenar por nome
  - [ ] `medialib_sort_by_artist()` — ordenar por artista
  - [ ] `medialib_sort_by_album()` — ordenar por álbum
  - [ ] `medialib_sort_by_duration()` — ordenar por duração
  - [ ] `medialib_sort_by_date()` — ordenar por data
  - [ ] `medialib_sort_by_rating()` — ordenar por rating
- [ ] Criar funções de filtragem:
  - [ ] `medialib_filter_by_type(type)` — filtrar por tipo
  - [ ] `medialib_filter_by_format(format)` — filtrar por formato
  - [ ] `medialib_filter_by_artist(artist)` — filtrar por artista
  - [ ] `medialib_filter_by_album(album)` — filtrar por álbum
  - [ ] `medialib_filter_by_rating(min_rating)` — filtrar por rating mínimo
- [ ] Criar funções de estatísticas:
  - [ ] `medialib_get_total_files()` — total de arquivos
  - [ ] `medialib_get_total_size()` — tamanho total em bytes
  - [ ] `medialib_get_total_duration()` — duração total
  - [ ] `medialib_get_format_stats()` — contagem por formato

### 5.4 Importação/Exportação

- [ ] Criar função `medialib_export_playlist(playlist, filepath)` para exportar como .m3u
- [ ] Criar função `medialib_import_playlist(filepath)` para importar .m3u
- [ ] Criar função `medialib_export_csv(filepath)` para exportar biblioteca
- [ ] Criar função `medialib_import_csv(filepath)` para importar biblioteca
- [ ] Criar função `medialib_backup(filepath)` para backup completo
- [ ] Criar função `medialib_restore(filepath)` para restaurar

---

## Fase 6: Interface TUI do Player ⬜

### 6.1 Janela Principal do Media Player

- [ ] Criar módulo `src/media/playerui.c` e `playerui.h`
- [ ] Criar janela "Media Player"
- [ ] Mostrar: Capa do álbum (BMP 64×64)
- [ ] Mostrar: Título da música
- [ ] Mostrar: Artista - Álbum
- [ ] Mostrar: Posição / Duração (MM:SS / MM:SS)
- [ ] Mostrar: Barra de progresso
- [ ] Mostrar: Volume (barra)
- [ ] Mostrar: Botões (Prev, Play/Pause, Stop, Next, Repeat, Shuffle)
- [ ] Integrar com window manager
- [ ] Registrar na taskbar com ícone de nota musical
- [ ] Registrar no menu Start
- [ ] Atalho F11 para abrir/fechar

### 6.2 Tela de Biblioteca

- [ ] Criar tela "Biblioteca de Mídia"
- [ ] Abas: Músicas, Álbuns, Artistas, Playlists
- [ ] Aba "Músicas":
  - [ ] Lista de todas as músicas
  - [ ] Colunas: Nome, Artista, Álbum, Duração, Rating
  - [ ] Ordenação por coluna (clique no cabeçalho)
  - [ ] Busca incremental (digitar para filtrar)
- [ ] Aba "Álbuns":
  - [ ] Grid de álbuns com capa
  - [ ] Ao selecionar: mostrar músicas do álbum
- [ ] Aba "Artistas":
  - [ ] Lista de artistas
  - [ ] Ao selecionar: mostrar álbuns/músicas
- [ ] Aba "Playlists":
  - [ ] Lista de playlists
  - [ ] Botão "Nova Playlist"
  - [ ] Botão "Importar Playlist"

### 6.3 Tela de Reprodução

- [ ] Criar tela "Tocando Agora"
- [ ] Mostrar: Capa do álbum (grande, 128×128)
- [ ] Mostrar: Título, Artista, Álbum
- [ ] Mostrar: Barra de progresso interativa
- [ ] Mostrar: Tempo atual / Tempo total
- [ ] Mostrar: Botões de controle ( Prev | << | Play/Pause | Stop | >> | Next )
- [ ] Mostrar: Volume slider
- [ ] Mostrar: Indicador de Repeat (Off/One/All)
- [ ] Mostrar: Indicador de Shuffle (On/Off)
- [ ] Navegação com setas para posição
- [ ] Tecla +/- para volume

### 6.4 Tela de Playlists

- [ ] Criar tela "Gerenciar Playlists"
- [ ] Lista de playlists criadas
- [ ] Botão "Criar Nova Playlist"
- [ ] Botão "Renomear"
- [ ] Botão "Excluir"
- [ ] Botão "Exportar (.m3u)"
- [ ] Botão "Importar (.m3u)"
- [ ] Ao selecionar playlist: mostrar músicas
- [ ] Botão "Adicionar Música" (abrir browser de arquivos)
- [ ] Botão "Remover Música"
- [ ] Botão "Mover para Cima/Baixo" (reordenar)

### 6.5 Tela de Configurações

- [ ] Criar painel de configurações do Media Player
- [ ] Opção: "Áudio" (output device, sample rate, buffer size)
- [ ] Opção: "Volume" (master volume, balance)
- [ ] Opção: "Equalizador" (bass, mid, treble, presets)
- [ ] Opção: "Reprodução" (repeat mode, shuffle, crossfade)
- [ ] Opção: "Biblioteca" (scan directories, auto-update)
- [ ] Opção: "Interface" (theme, font size)
- [ ] Salvar em `/media/config.txt`

### 6.6 Tela de Equalizador

- [ ] Criar tela "Equalizador"
- [ ] 10 bandas: 31Hz, 62Hz, 125Hz, 250Hz, 500Hz, 1kHz, 2kHz, 4kHz, 8kHz, 16kHz
- [ ] Slider para cada banda (-12dB a +12dB)
- [ ] Presets: Flat, Rock, Pop, Jazz, Classical, Bass Boost, Treble Boost
- [ ] Botão "Salvar Preset"
- [ ] Botão "Restaurar Padrão"
- [ ] Aplicar equalização em tempo real via mixer

### 6.7 Integração com Shell

- [ ] Comando `player` — abrir Media Player
- [ ] Comando `play <file>` — tocar arquivo (já existe)
- [ ] Comando `stop` — parar (já existe)
- [ ] Comando `pause` — pausar
- [ ] Comando `resume` — retomar
- [ ] Comando `next` — próxima faixa
- [ ] Comando `prev` — faixa anterior
- [ ] Comando `volume <0-100>` — definir volume
- [ ] Comando `mute` — silenciar/desilenciar
- [ ] Comando `eq <preset>` — aplicar preset de equalizador
- [ ] Comando `playlist list` — listar playlists
- [ ] Comando `playlist create <name>` — criar playlist
- [ ] Comando `playlist add <name> <file>` — adicionar à playlist
- [ ] Comando `playlist play <name>` — tocar playlist
- [ ] Comando `library scan` — escanear biblioteca
- [ ] Comando `library list` — listar biblioteca
- [ ] Comando `gallery` — abrir image viewer

---

## Fase 7: Configurações e Equalização ⬜

### 7.1 Configurações de Áudio

- [ ] Criar struct `audio_config_t`:
  ```
  - output_device    = enum (AC97, PC_SPEAKER)
  - sample_rate      = uint32_t (8000, 11025, 22050, 44100, 48000)
  - bits_per_sample  = uint8_t (8, 16)
  - channels         = uint8_t (1, 2)
  - buffer_size      = uint32_t (1024, 2048, 4096, 8192)
  - master_volume    = uint8_t (0-100)
  - balance          = int8_t (-100 a 100)
  ```
- [ ] Criar função `audio_config_load()` para carregar
- [ ] Criar função `audio_config_save()` para salvar
- [ ] Criar função `audio_config_set_default()` para padrão
- [ ] Salvar em `/media/audio-config.txt`

### 7.2 Equalizador

- [ ] Criar módulo `src/media/equalizer.c` e `equalizer.h`
- [ ] Criar struct `equalizer_t`:
  ```
  - bands[10]       = int8_t (-12 a +12 dB)
  - preset          = enum (CUSTOM, FLAT, ROCK, POP, JAZZ, CLASSICAL, etc.)
  - enabled         = bool
  ```
- [ ] Implementar filtros IIR (biquad) para cada banda:
  - [ ] Low shelf (31Hz, 62Hz)
  - [ ] Peaking (125Hz - 8kHz)
  - [ ] High shelf (16kHz)
- [ ] Criar função `eq_set_band(band, gain)` para configurar banda
- [ ] Criar função `eq_apply(pcm_buffer, samples)` para aplicar
- [ ] Criar função `eq_set_preset(preset)` para aplicar preset
- [ ] Criar função `eq_get_preset_names()` para listar presets
- [ ] Integrar com mixer (aplicar equalização antes da mixagem)

### 7.3 Crossfade

- [ ] Criar módulo `src/media/crossfade.c` e `crossfade.h`
- [ ] Criar struct `crossfade_t`:
  ```
  - enabled         = bool
  - duration_ms     = uint32_t (1000-5000, default 3000)
  ```
- [ ] Criar função `crossfade_set_duration(ms)` para configurar
- [ ] Criar função `crossfade_enable()` / `crossfade_disable()`
- [ ] Implementar crossfade:
  - [ ] Quando próximo track está para começar
  - [ ] Fade out do track atual (volume 100% → 0%)
  - [ ] Fade in do próximo track (volume 0% → 100%)
  - [ ] Mixar os dois durante o período de transição
- [ ] Integrar com playlist (chamar quando mudar de faixa)

### 7.4 Visualização de Áudio

- [ ] Criar módulo `src/media/visualizer.c` e `visualizer.h`
- [ ] Criar visualizador de barras (spectrum analyzer):
  - [ ] 16 barras verticais
  - [ ] Cores baseadas na amplitude (verde/amarelo/vermelho)
  - [ ] Atualizar a cada 50ms
- [ ] Criar visualizador de onda (waveform):
  - [ ] Linha contínu representando a forma de onda
  - [ ] Cor baseada no volume
- [ ] Criar visualizador circular (radial):
  - [ ] Barras dispostas em círculo
  - [ ] Efeito de rotação
- [ ] Integrar com Media Player (mostrar durante reprodução)
- [ ] Criar opção de desabilitar visualização (economia de CPU)

### 7.5 Integração com Settings

- [ ] Adicionar categoria "Mídia" no Settings
- [ ] Opção: "Device de saída" (AC97/PC Speaker)
- [ ] Opção: "Volume padrão" (0-100)
- [ ] Opção: "Repetição padrão" (Off/One/All)
- [ ] Opção: "Shuffle padrão" (On/Off)
- [ ] Opção: "Equalizador" (on/off, preset)
- [ ] Opção: "Crossfade" (on/off, duração)
- [ ] Opção: "Scanner automático" (on/off)
- [ ] Opção: "Diretórios de mídia" (adicionar/remover)

---

## Limitações Técnicas

| Limite | Valor | Descrição |
|--------|-------|-----------|
| Máximo de arquivos na biblioteca | 256 | Array estático |
| Máximo de playlists | 16 | Array estático |
| Máximo de faixas por playlist | 64 | Array estático |
| Máximo de canais mixer | 8 | Mixagem limitada |
| Tamanho do buffer de áudio | 8KB | AC97 DMA buffer |
| Sample rates suportados | 8-48kHz | Limitado por AC97 |
| Bits por sample | 8, 16 | AC97 suporta até 20 |
| FPS máximo de vídeo | 30 | Limitado por VESA + CPU |
| Resolução de vídeo | até 1920×1080 | VESA framebuffer |
| Tamanho máximo de imagem | 4MB | Memória disponível |
| Tamanho máximo de áudio | 64MB | Memória disponível |
| Tamanho máximo de vídeo | 100MB | Memória disponível |
| Formatos de áudio | WAV, MP3, OGG, FLAC | Decoders básicos |
| Formatos de vídeo | AVI, WebM | Containers simples |
| Formatos de imagem | BMP, PNG, JPEG, GIF | Loaders básicos |
| Sem DRM | Nenhum | Sem suporte a mídia protegida |
| Sem streaming | Nenhum | Sem playback de rede |
| Sem legendas | Nenhum | Sem suporte a SRT/VTT |
| Sem codecs de vídeo avançados | Nenhum | Sem H.264/HEVC/VP9 |
| Sem aceleração por GPU | Nenhum | Decodificação por CPU |

---

## Notas de Implementação

1. **WAV como formato nativo** — O WAV é o formato mais simples (PCM uncompressed). É o único suportado nativamente. MP3/OGG/FLAC requerem decoders complexos.

2. **BMP como formato de imagem** — BMP é simples (sem compressão ou RLE simples). PNG requer inflate (zlib). JPEG requer Huffman + DCT. GIF requer LZW.

3. **AVI como container de vídeo** — AVI é um container simples (RIFF-based). Containers como MP4/MKV são mais complexos (atoms/EBML).

4. **Mixagem limitada** — O mixer suporta 8 canais simultâneos. Cada canal requer buffer de áudio separado. A mixagem é feita por software (soma de samples com clamp).

5. **Equalizador por software** — O equalizador usa filtros IIR (biquad) implementados por software. Isso consome CPU significativa. Em hardware real, o AC97 pode ter equalização nativa.

6. **Sem aceleração por GPU** — Toda decodificação de vídeo é feita por CPU. Resoluções altas (720p+) podem não ser viáveis em CPUs lentas.

7. **Integração existente** — O AC97 driver, WAV parser, BMP loader e Media Player já existem. O roadmap estende esses módulos com novos formatos e funcionalidades.

8. **Memória** — Cada frame de vídeo 720p (1280×720×3) usa ~2.7MB. Com double buffering, são ~5.4MB. O decoder MP3 usa ~64KB de tabela. Total para vídeo: ~10MB de RAM.

---

## Referências

- `src/drivers/ac97.c` — AC97 audio driver (197 linhas)
- `src/drivers/speaker.c` — PC Speaker driver (61 linhas)
- `src/fs/wav.c` — WAV parser (109 linhas)
- `src/fs/bmp.c` — BMP loader (194 linhas)
- `src/shell/mediaplayer.c` — Media player app (240 linhas)
- `src/drivers/vesa.c` — VESA framebuffer (392 linhas)
- `src/drivers/font.c` — Bitmap font (119 linhas)
- `src/fs/fs.c` — Unified FS API (194 linhas)
- `src/memory/memory.c` — Memory manager (206 linhas)
- `src/shell/shell.c` — Shell commands (518 linhas)
- `src/settings/settings.c` — Settings panel (549 linhas)
- ISO/IEC 11172-3 — MPEG-1 Audio Layer III (MP3)
- RFC 3533 — OGG Encapsulation
- FLAC Specification — Free Lossless Audio Codec
- PNG Specification — Portable Network Graphics
- ITU-T T.81 — JPEG Compression
- GIF89a Specification — Graphics Interchange Format
- AVI RIFF Specification — Audio Video Interleaved
