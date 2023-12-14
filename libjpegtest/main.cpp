/**
 * libjpegのメモリ読み出し＆書き出しサンプル
 *
 *   Linuxだと、大抵libjpegが組み込まれているので、
 *   Windowsにて動作する、ライブラリのビルドと読み出し/書き出しをするサンプルを書いた。
 *
 *   Note: jconfig.vcは使用していない。
 *         WindowsのAPIを使っている関係で、open()に渡すフラグが
 *         POSIX のものと微妙に違ったり(O_CREAT ⇒ _O_CREAT等)
 *         statが_statだったりする。
 *   オリジナルで boolean となっていた真偽値型は stdbool.h をインクルードして bool を使う用に変えた。
 *   オリジナルで jpeg_mem_src / jpeg_mem_dest が無かったので、 libjpeg9 からコピーしてきた。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>


#include "libjpeg/jpeglib.h"

struct image {
    int width; // イメージ幅
    int height; // イメージ高さ
    int bytes_per_pixel; // 1ピクセルあたりのバイト数

    uint8_t* raster; // ラスターデータ
};

static int create_empty_image(struct image* image);
static int read_jpeg(const char* path, struct image* image);
static uint8_t* read_file_all(const char* path, size_t* psize);
static int write_to_jpeg(const char* path, const struct image* image);
static int write_to_file(const char* path, const uint8_t* data, size_t length);

int main(int ac, char **av)
{
#if 0
    struct image image;
    image.raster = NULL;
    int s = create_empty_image(&image);
    if (image.raster != NULL) {
        for (int y = 0; y < image.height; y++) {
            int x = y % image.width;
            uint8_t* wp = image.raster + image.width * image.bytes_per_pixel * y
                + x * image.bytes_per_pixel;
            for (int bits = 0; bits < image.bytes_per_pixel; bits++) {
                wp[bits] = 0xFF;
            }
        }
    }
#else
    struct image image;
    image.raster = NULL;

    const char* src_path = (ac >= 2) ? av[1] : "test.jpg";

    int s = read_jpeg(src_path, &image);
#endif
    if (s == 0) {
        write_to_jpeg("output.jpg", &image);
    }

    if (image.raster != NULL) {
        free(image.raster);
        image.raster = NULL;
    }

    return 0;
}

/**
 * 空のイメージを作成する。
 *
 * @param image 初期化するimageオブジェクト
 * @retval 0 成功
 * @retval -1 失敗
 */
static int create_empty_image(struct image* image) {
    image->bytes_per_pixel = 3;
    image->width = 1280;
    image->height = 720;
    size_t raster_size = 1280 * 720 * 3;
    image->raster = (uint8_t*)(malloc(raster_size));
    if (image->raster == NULL) {
        return -1;
    }

    memset(image->raster, 0x00, raster_size);

    return 0;
}

/**
 * JPEGイメージを読み込む。
 *
 * @param path ファイルパス
 * @param image 読み込み先のimageオブジェクト
 * @retval 0 成功
 * @retval 0以外 エラー
 */
static int read_jpeg(const char* path, struct image* image)
{
    size_t data_size;

    // ファイル読み出し。
    // pathで指定されたファイルを全部読み込む。
    // ファイルサイズがうんGBとかだと
    // おそらくメモリアロケーションに失敗するが、指定されない想定。
    uint8_t* bufp = read_file_all(path, &data_size);
    if (bufp == NULL) {
        return errno;
    }

    // 初期化
    struct jpeg_error_mgr jerr;
    struct jpeg_decompress_struct cinfo;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, bufp, data_size);

    // 読み出した画像を取得する形式の指定
    cinfo.out_color_space = JCS_RGB; // RGBに変換しつつ取得
    cinfo.out_color_components = 3; // 3バイト/ピクセル -> 24bit RGB

    if (jpeg_read_header(&cinfo, true) != 1) {
        fprintf(stderr, "Reading header failure.\n");
        free(bufp);
        return EIO;
    }

    jpeg_start_decompress(&cinfo);

    fprintf(stdout, "%s\n", path);
    fprintf(stdout, "  width = %d\n", cinfo.image_width);
    fprintf(stdout, "  height = %d\n", cinfo.image_height);
    fprintf(stdout, "  color_components = %d\n", cinfo.out_color_components);
    fprintf(stdout, "  color_space = %d\n", cinfo.out_color_space);

    // 読み出し用バッファを確保
    int width = cinfo.output_width;
    int height = cinfo.output_height;
    int bytes_per_pixel = cinfo.output_components;
    int raster_size = width * height * bytes_per_pixel;

    uint8_t* raster = (uint8_t*)(malloc(raster_size));
    if (raster == NULL) {
        char errmsg_buf[256];
        strerror_s(errmsg_buf, sizeof(errmsg_buf), errno);
        fprintf(stderr, "Could not allocate moery. (size=%d, %s)\n", raster_size, errmsg_buf);
        free(bufp); // ファイルから読み出したバッファは解放
        return ENOMEM;
    }

    // 展開処理
    int line_size = width * bytes_per_pixel;
    while (cinfo.output_scanline < cinfo.output_height) {
        uint8_t* lines[1] = {
            raster + (line_size * cinfo.output_scanline)
        };
        jpeg_read_scanlines(&cinfo, lines, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    free(bufp); // ファイルから読み出したバッファは解放

    image->width = width;
    image->height = height;
    image->bytes_per_pixel = bytes_per_pixel;
    image->raster = raster;

    printf("jpeg decompress done.\n");

    return 0;
}

/**
 * pathで指定されたファイルを読み出す。
 *
 * @param path ファイルパス
 * @param psize 読み出したサイズを格納する変数
 * @retval 読み出したデータのポインタ。使用後はfree()で解放すること。
 * @retval NULL
 */
static uint8_t* read_file_all(const char* path, size_t* psize)
{
    errno_t err = _access_s(path, 0x04); // 読み出しアクセスの確認
    if (err != 0) {
        char errmsg_buf[256];
        strerror_s(errmsg_buf, sizeof(errmsg_buf), err);
        fprintf(stderr, "Could not access to %s. (%s)\n", path, errmsg_buf);
        return NULL;
    }

    struct _stat file_stat;

    if (_stat(path, &file_stat) != 0) {
        char errmsg_buf[256];
        strerror_s(errmsg_buf, sizeof(errmsg_buf), errno);
        fprintf(stderr, "Could not get file status. (%s)\n", errmsg_buf);
        return NULL;
    }

    if (file_stat.st_size >= (1 * 1024 * 1024)) { // 1MiB以上のファイル？
        fprintf(stderr, "Too large file for this program. (%s)\n", path);
        return NULL;
    }

    uint8_t* bufp = (uint8_t*)(malloc(file_stat.st_size));
    if (bufp == NULL) {
        char errmsg_buf[256];
        strerror_s(errmsg_buf, sizeof(errmsg_buf), errno);
        fprintf(stderr, "Could not allocate memory. (%s)\n", errmsg_buf);
        return NULL;
    }

    int fd;
    err = _sopen_s(&fd, path, _O_BINARY | _O_RDONLY, _SH_DENYRW, 0);
    if (err != 0) {
        char errmsg_buf[256];
        strerror_s(errmsg_buf, sizeof(errmsg_buf), err);
        fprintf(stderr, "Could not open %s. (%s)\n", path, errmsg_buf);
        free(bufp);
        return NULL;
    }

    uint8_t* wp = bufp;
    size_t read_size = 0u;
    unsigned int left = (unsigned int)(file_stat.st_size);
    while (left > 0) {
        int s = _read(fd, wp, left);
        if (s > 0) {
            wp += s;
            read_size += (size_t)(s);
            left -= s;
        }
        else {
            char errmsg_buf[256];
            strerror_s(errmsg_buf, sizeof(errmsg_buf), errno);
            fprintf(stderr, "Reading file error. (%s)\n", errmsg_buf);
            free(bufp);
            _close(fd);
            return NULL;
        }
    }

    printf("Read file done. %dbytes.\n", (int)(read_size));

    (*psize) = read_size;

    _close(fd);

    return bufp;
}

/**
 * JPEGファイルを書き出す。
 *
 * このサンプルはRGBのイメージを受け取って書き出すようになっている。
 *
 * @param path ファイルパス
 * @param image 画像
 * @retval 0 成功
 * @retval エラー番号 失敗した場合
 */
static int write_to_jpeg(const char* path, const struct image* image)
{
    // 書き出し用バッファ確保
    // Note : jpeg_mem_dest内で自動的にリサイズを許可するなら、
    //        jpeg_mem_destのメモリ管理に則ったメモリ確保をする必要がある。
    //        jpeg_mem_dest内でmalloc()/free()を使ってるなら、
    //        イニシャルバッファにmalloc()した物を渡す
    //        さもないと、バッファを伸張するとき、このアドレスに対してfree()が呼ばれて
    //        予期せぬ不具合を起こす。
    int raster_size = image->width * image->bytes_per_pixel * image->height;
    uint8_t* wbuf = (uint8_t*)(malloc(raster_size));
    if (wbuf == NULL) {
        int errsv = errno;
        char errmsg_buf[256];
        strerror_s(errmsg_buf, sizeof(errmsg_buf), errsv);
        fprintf(stderr, "Could not allocate memory. (%d %s)\n", raster_size, errmsg_buf);
        return errsv;
    }

    // 初期化
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    size_t wsize = raster_size;
    jpeg_mem_dest(&cinfo, &wbuf, &wsize, true);

    // 入力元のイメージ設定
    cinfo.image_width = image->width;
    cinfo.image_height = image->height;
    cinfo.input_components = image->bytes_per_pixel;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 75, true); // Quality.

    // JPEG圧縮開始
    jpeg_start_compress(&cinfo, true);

    int row_stride = image->width * 3;
    int row_size = image->width * image->bytes_per_pixel;
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row_pointer[1] = { image->raster + row_size * cinfo.next_scanline };
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);

    // ファイルに書き出し
    int s = write_to_file(path, wbuf, wsize);
    if (s != 0) {
        char errmsg_buf[256];
        strerror_s(errmsg_buf, sizeof(errmsg_buf), s);
        fprintf(stderr, "Could not open %s. (%s)\n", path, errmsg_buf);
    }

    jpeg_destroy_compress(&cinfo);
    free(wbuf);

    return s;
}

/**
 * ファイルを書き出す。
 *
 * @param path ファイルパス
 * @param
 */
static int write_to_file(const char* path, const uint8_t* data, size_t length) {
    int retval = 0;

    int fd;
    errno_t err = _sopen_s(&fd, path, _O_CREAT | _O_TRUNC | _O_BINARY | _O_WRONLY, _SH_DENYRW, _S_IREAD | _S_IWRITE);
    if (err != 0) {
        return err;
    }

    const uint8_t* rp = data;
    int wrote_len = 0;
    unsigned int left = (unsigned int)(length);
    while (left > 0) {
        int ret = _write(fd, rp, left);
        if (ret > 0) {
            rp += ret;
            wrote_len += ret;
            left -= ret;
        }
        else {
            retval = errno;
            char errmsg_buf[256];
            strerror_s(errmsg_buf, sizeof(errmsg_buf), retval);
            fprintf(stderr, "Write file failure. (%s)\n", errmsg_buf);
            break;
        }
    }
    _close(fd);

    return retval;

}
