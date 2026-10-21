/* Generate a 3-page A4 600 dpi 8-bit sgray PWG raster for the integration test.
 * usage: mkpwg WIDTH OUT.pwg
 * Content: horizontal gray gradient black->white (dithered -> non-trivial JBIG), a pure black
 * band in the 16 rightmost raster columns and a black top-left square. MIT License. */
#include <cups/raster.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: mkpwg WIDTH OUT.pwg\n");
    return 2;
  }
  unsigned width = (unsigned)atoi(argv[1]);
  int fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    perror(argv[2]);
    return 1;
  }
  cups_raster_t *ras = cupsRasterOpen(fd, CUPS_RASTER_WRITE_PWG);
  cups_page_header2_t h;
  if (!cupsRasterInitPWGHeader(&h, pwgMediaForPWG("iso_a4_210x297mm"), "sgray_8", 600, 600, "one-sided", "normal")) {
    fprintf(stderr, "mkpwg: cupsRasterInitPWGHeader failed: %s\n", cupsLastErrorString());
    return 1;
  }
  h.cupsWidth = width;
  h.cupsBytesPerLine = width;
  h.cupsInteger[CUPS_RASTER_PWG_TotalPageCount] = 3;
  unsigned char *line = malloc(width);
  for (int page = 0; page < 3; page++) {
    if (!cupsRasterWriteHeader2(ras, &h)) {
      fprintf(stderr, "mkpwg: write header failed\n");
      return 1;
    }
    for (unsigned y = 0; y < h.cupsHeight; y++) {
      for (unsigned x = 0; x < width; x++)
        line[x] = (unsigned char)(x * 255 / width); /* black left -> white right */
      for (unsigned x = width - 16; x < width; x++)
        line[x] = 0;
      if (y < 600)
        memset(line, 0, 600);
      cupsRasterWritePixels(ras, line, width);
    }
  }
  free(line);
  cupsRasterClose(ras);
  close(fd);
  printf("mkpwg: %s: 3 pages %ux%u 8-bit sgray\n", argv[2], width, h.cupsHeight);
  return 0;
}
