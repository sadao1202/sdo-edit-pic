// stb_image / stb_image_writeの実装マクロ定義専用TU。
// image_io.cpp（Win32の_wfopen依存）とimage_codec.cpp（Win32非依存）の両方から
// stb関数を使うため、実装本体はこの独立したファイルに一本化する。
// これにより、image_codec.cpp + stb_impl.cpp + image_document.cppの組み合わせだけで
// WSL上のネイティブLinuxビルド（image_codec_tests）が可能になる。
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
