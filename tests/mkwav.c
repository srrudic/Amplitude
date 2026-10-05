/* Writes a WAV test tone: mkwav <file> <seconds> <hz> <amplitude 0..32767>.
 * Used by gui_test.sh; amplitude 0 gives digital silence. */
#include "test.h"

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: mkwav <file> <seconds> <hz> <amplitude>\n");
        return 2;
    }
    test_write_wav(argv[1], atof(argv[2]), atof(argv[3]), atoi(argv[4]));
    return 0;
}
