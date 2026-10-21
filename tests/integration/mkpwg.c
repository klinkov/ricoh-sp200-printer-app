/* Generate an 8-bit sgray 600 dpi PWG raster for the integration test.
 * usage: mkpwg WIDTH OUT.pwg [MEDIA [PAGES [PATTERN]]]
 *   WIDTH   raster width in pixels; 0 keeps the width from cupsRasterInitPWGHeader
 *   MEDIA   PWG media name (default iso_a4_210x297mm); the height always comes from the header
 *   PAGES   default 3
 *   PATTERN band (default): horizontal gray gradient black->white (dithered -> non-trivial JBIG), a
 *           pure black band in the 16 rightmost raster columns and a black top-left square;
 *           gray127: flat gray 127 (threshold -> all black, Floyd-Steinberg -> ~50% dots);
 *           text: white page with a few black bars (tiny JBIG).
 * MIT License. */
#include <cups/raster.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 3 || argc > 6) {
    fprintf(stderr, "usage: mkpwg WIDTH OUT.pwg [MEDIA [PAGES [band|gray127|text]]]\n");
    return 2;
  }
  unsigned width = (unsigned)atoi(argv[1]);
  const char *media = argc > 3 ? argv[3] : "iso_a4_210x297mm";
  int pages = argc > 4 ? atoi(argv[4]) : 3;
  const char *pattern = argc > 5 ? argv[5] : "band";
  pwg_media_t *pwg = pwgMediaForPWG(media);
  if (!pwg || pages < 1) {
    fprintf(stderr, "mkpwg: bad media or page count\n");
    return 2;
  }
  int fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    perror(argv[2]);
    return 1;
  }
  cups_raster_t *ras = cupsRasterOpen(fd, CUPS_RASTER_WRITE_PWG);
  cups_page_header2_t h;
  if (!cupsRasterInitPWGHeader(&h, pwg, "sgray_8", 600, 600, "one-sided", "normal")) {
    fprintf(stderr, "mkpwg: cupsRasterInitPWGHeader failed: %s\n", cupsLastErrorString());
    return 1;
  }
  if (width) {
    h.cupsWidth = width;
    h.cupsBytesPerLine = width;
  }
  width = h.cupsWidth;
  h.cupsInteger[CUPS_RASTER_PWG_TotalPageCount] = (unsigned)pages;
  unsigned char *line = malloc(width);
  for (int page = 0; page < pages; page++) {
    if (!cupsRasterWriteHeader2(ras, &h)) {
      fprintf(stderr, "mkpwg: write header failed\n");
      return 1;
    }
    for (unsigned y = 0; y < h.cupsHeight; y++) {
      if (!strcmp(pattern, "gray127")) {
        memset(line, 127, width);
      } else if (!strcmp(pattern, "text")) {
        memset(line, 255, width);
        if (y % 600 >= 300 && y % 600 < 340)
          memset(line + 300, 0, width / 2);
      } else {
        for (unsigned x = 0; x < width; x++)
          line[x] = (unsigned char)(x * 255 / width); /* black left -> white right */
        for (unsigned x = width - 16; x < width; x++)
          line[x] = 0;
        if (y < 600)
          memset(line, 0, 600);
      }
      cupsRasterWritePixels(ras, line, width);
    }
  }
  free(line);
  cupsRasterClose(ras);
  close(fd);
  printf("mkpwg: %s: %d pages %ux%u 8-bit sgray\n", argv[2], pages, width, h.cupsHeight);
  return 0;
}
