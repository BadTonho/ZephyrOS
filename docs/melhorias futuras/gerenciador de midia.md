O suporte de mídia do Windows depende de três coisas diferentes:

Extensão do arquivo, como .mp4 ou .mkv.
Contêiner, que guarda áudio, vídeo, legendas e metadados.
Codec usado dentro do arquivo, como H.264, HEVC ou AV1.

Por isso, dois arquivos .mp4 podem ter resultados diferentes: um pode usar H.264 e abrir normalmente, enquanto outro usa HEVC e exige uma extensão adicional.

1. Áudio suportado nativamente

O Windows 10 e 11 possuem suporte integrado para os formatos de áudio mais comuns:

Formato	Extensões comuns	Situação
MP3	.mp3	Nativo
MP2	.mp2, .mpa	Nativo
AAC	.aac, .adts, .adt	Nativo
M4A	.m4a	Nativo
WAV/PCM	.wav	Nativo
WMA	.wma	Nativo
FLAC	.flac	Nativo
ALAC	geralmente .m4a	Nativo
AIFF	.aif, .aiff, .aifc	Suportado pelo Windows Media Player
MIDI	.mid, .midi, .rmi	Suportado
AMR	.amr e dentro de 3GP	Nativo
Áudio 3GP	.3gp, .3g2	Nativo
Áudio de CD	.cda	Suportado
AU/SND	.au, .snd	Suporte legado

A Microsoft lista MP3, MP2, AAC, M4A, FLAC, ALAC, WAV, WMA, 3GP, 3G2 e AMR entre os formatos de áudio incluídos de fábrica.

Áudio que pode exigir extensão ou codec
Formato/codec	Situação
Ogg Vorbis	Extensão de Mídia Web ou outro player
Opus	Pode depender do aplicativo/extensão
AC-3 / Dolby Digital	Depende da versão e do fabricante
DTS	Normalmente depende de software ou licença adicional
Dolby TrueHD	Geralmente exige player/codec externo
DTS-HD	Geralmente exige player/codec externo
DSD	Não é suporte nativo comum
APE/Monkey’s Audio	Normalmente exige player externo

Uma mudança importante: instalações novas do Windows 11 24H2 ou posterior não incluem obrigatoriamente o codec AC-3/Dolby Digital. Computadores atualizados de versões anteriores podem mantê-lo, e alguns fabricantes o instalam de fábrica.

2. Vídeo suportado nativamente

Os codecs de vídeo incluídos normalmente são:

Codec	Onde costuma aparecer	Situação
H.264/AVC	MP4, MOV, M4V, 3GP	Nativo
H.263	3GP e vídeos antigos	Nativo
WMV	WMV, ASF	Nativo
VC-1	WMV, ASF, alguns Blu-rays	Nativo
DV	AVI/DV	Nativo
VP8	WebM	Nativo
Motion JPEG	AVI, MOV	Nativo

A Microsoft lista H.264, H.263, VC-1, WMV, DV, VP8 e Motion JPEG como codecs de vídeo incluídos de fábrica.

Vídeo que normalmente requer extensão
Codec	Extensão necessária ou situação
HEVC/H.265	Extensão de Vídeo HEVC
AV1	Extensão de Vídeo AV1
VP9	Extensão de Vídeo VP9
MPEG-1 vídeo	Extensão MPEG-2
MPEG-2 vídeo	Extensão MPEG-2
Theora	Extensão de Mídia Web

Essas extensões podem ser instaladas pela Microsoft Store. A Microsoft disponibiliza extensões específicas para MPEG-2, HEVC, VP9, AV1 e formatos Web/OGG.

3. Contêineres de vídeo e áudio

O contêiner é o “arquivo externo”. Ele pode guardar diferentes codecs dentro dele.

Contêineres amplamente suportados
Contêiner	Extensões
MPEG-4	.mp4, .m4v, .mp4v
QuickTime	.mov
AVI	.avi
Windows Media/ASF	.wmv, .asf, .wm
3GPP	.3gp, .3gpp, .3g2, .3gp2
MPEG Program Stream	.mpg, .mpeg, .mpe
MPEG Transport Stream	.m2ts, .mts, .ts
WebM	.webm, dependendo dos codecs/extensões
Ogg	.ogg, .ogv, com Extensão de Mídia Web

O Windows Media Player também reconhece MP4, MOV, AVI, WMV, ASF, 3GP e MPEG, desde que o codec interno seja compatível.

MKV — Matroska

O .mkv é um contêiner bastante comum. Versões modernas do Windows e do Media Player conseguem abrir muitos arquivos MKV, mas o resultado depende dos codecs internos.

Exemplos:

MKV com H.264 e AAC: geralmente funciona.
MKV com HEVC: precisa do codec HEVC.
MKV com AV1: precisa do suporte AV1.
MKV com DTS: pode abrir o vídeo e ficar sem áudio.
MKV com legendas avançadas: o suporte pode variar.

Portanto, “suportar MKV” não significa automaticamente suportar tudo que pode existir dentro dele.

4. Imagens suportadas

O aplicativo Fotos, o Explorador e os componentes gráficos do Windows normalmente reconhecem:

Formato	Extensões	Situação
JPEG	.jpg, .jpeg, .jpe	Nativo
PNG	.png	Nativo
BMP	.bmp, .dib	Nativo
GIF	.gif	Nativo
TIFF	.tif, .tiff	Nativo
ICO	.ico	Nativo
WebP	.webp	Suportado em versões e aplicativos modernos
JPEG XR	.jxr, .wdp, .hdp	Suporte do Windows Imaging Component
AVIF	.avif	Pode depender da Extensão AV1
HEIF/HEIC	.heif, .heic	Requer extensões
RAW de câmeras	.cr2, .cr3, .nef, .arw, .dng etc.	Depende da extensão RAW e do modelo
HEIC e HEIF

Arquivos de iPhone normalmente usam:

Foto: .heic ou .heif
Vídeo: HEVC/H.265

Para abrir completamente esses arquivos no Fotos, pode ser necessário instalar:

Extensões de Imagem HEIF
Extensões de Vídeo HEVC

A própria Microsoft informa que, em alguns casos, as duas extensões são necessárias mesmo para visualizar fotografias HEIC.

5. Listas de reprodução e arquivos auxiliares

O Windows Media Player também reconhece formatos que não contêm necessariamente a mídia em si:

Tipo	Extensões
Playlist do Windows Media	.wpl
Playlist M3U	.m3u, possivelmente .m3u8
Metarquivo Windows Media	.asx, .wax, .wvx, .wmx
Pacote Windows Media	.wmd
Gravação antiga do Media Center	.dvr-ms

Esses arquivos normalmente apontam para músicas, vídeos ou transmissões armazenadas em outros locais.

6. Legendas

O suporte a legendas varia de acordo com o player e o contêiner.

Formatos comuns:

.srt
.vtt
.ssa
.ass
Legenda incorporada em MP4 ou MKV
Closed captions incorporadas em determinados arquivos

O Media Player moderno consegue trabalhar com legendas em vários cenários, mas formatos avançados, fontes personalizadas, animações e posicionamento complexo podem não ser reproduzidos corretamente. Players como VLC e MPC costumam oferecer suporte mais amplo.

7. Reprodução de DVD e Blu-ray
DVD de vídeo

O Windows 10 e 11 não incluem reprodução completa de DVD comercial de fábrica no Media Player tradicional. A Microsoft informa que a reprodução de DVD não está incluída no Windows Media Player Legacy.

Para DVD, normalmente é necessário:

Aplicativo de DVD da Microsoft ou do fabricante.
VLC ou outro player compatível.
Unidade de DVD.
Decodificador MPEG-2 compatível.
Blu-ray

O Windows não oferece suporte nativo completo para reprodução de discos Blu-ray comerciais.

Isso ocorre por causa de:

Proteção contra cópia.
Licenças.
Menus Blu-ray.
Codecs de áudio e vídeo.
Chaves de descriptografia.

Normalmente é necessário software específico fornecido com a unidade ou adquirido separadamente.

8. Streaming e DRM

O Windows também possui suporte para mídia protegida e streaming por meio de:

Microsoft Edge.
Media Foundation.
PlayReady DRM.
Widevine dentro de navegadores compatíveis.
Aplicativos como Netflix, Prime Video e Disney+.
Serviços Xbox e Microsoft Store.
Streaming HTTP, HLS e MPEG-DASH, dependendo do aplicativo.

A resolução máxima depende de:

Aplicativo ou navegador.
Codec.
DRM.
Monitor.
Conexão HDCP.
GPU.
Plano do serviço.
Hardware de decodificação.
9. Aceleração por hardware

O Windows pode usar a GPU para decodificar mídia, diminuindo o consumo de CPU.

Dependendo da placa de vídeo, é possível acelerar:

H.264.
HEVC/H.265.
VP9.
AV1.
MPEG-2.
VC-1.
Processamento HDR.
Conversão de cores.
Redimensionamento do vídeo.

Ter o codec instalado não garante aceleração por hardware. Caso a GPU não possua um decodificador compatível, o vídeo pode ser processado pela CPU.

10. Resoluções e HDR

O Windows consegue trabalhar com:

720p.
1080p.
1440p.
4K.
8K, dependendo do codec e hardware.
HDR10.
Vídeos de alta taxa de quadros.
Conteúdo com profundidade de 10 bits.
Taxa de atualização variável em jogos e vídeos compatíveis.

Para 4K, 8K ou HDR, são necessários:

GPU compatível.
Driver atualizado.
Codec adequado.
Cabo e conexão apropriados.
Monitor ou televisão compatível.
Em alguns serviços, HDCP e DRM adequados.
11. Edições Windows N

As edições Windows 10 N e Windows 11 N são distribuídas sem vários componentes de mídia.

Elas podem não incluir inicialmente:

Media Player.
Codecs de áudio e vídeo.
Gravador de Som.
Componentes de streaming.
Recursos de câmera e comunicação dependentes da pilha multimídia.

Nessas edições, é necessário instalar o Media Feature Pack como recurso opcional.

Resumo prático
Funciona normalmente de fábrica
MP3
WAV
WMA
AAC
M4A
FLAC
ALAC
MP4 com H.264
MOV com H.264
AVI com codec compatível
WMV
JPEG
PNG
BMP
GIF
TIFF
Pode precisar de extensão
HEVC/H.265
HEIC/HEIF
AV1
AVIF
VP9
MPEG-2
OGG/Vorbis/Theora
Alguns formatos RAW
Geralmente é melhor usar outro player
MKV com codecs incomuns.
DTS e DTS-HD.
Dolby TrueHD.
Blu-ray comercial.
Vídeos antigos com codecs raros.
Legendas ASS/SSA complexas.
APE, DSD e formatos de áudio especializados.
Arquivos RealMedia .rm, .ra e .ram.

A regra mais importante é: a extensão não garante compatibilidade. Um .mp4, .avi ou .mkv só será reproduzido se o Windows também reconhecer os codecs de áudio e vídeo usados dentro dele.