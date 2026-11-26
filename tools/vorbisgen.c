/* Host tool: synthesize a small Ogg Vorbis clip (8 kHz mono sine) using
 * libvorbis. Usage: vorbisgen <out.ogg> [num_samples] */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vorbis/vorbisenc.h>

#define RATE 8000

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: vorbisgen <out.ogg> [num_samples]\n");
        return 1;
    }
    int total = argc > 2 ? atoi(argv[2]) : 880;
    if (total < 64) total = 64;

    FILE *out = fopen(argv[1], "wb");
    if (!out) {
        perror("fopen");
        return 1;
    }

    vorbis_info vi;
    vorbis_info_init(&vi);
    if (vorbis_encode_init_vbr(&vi, 1, RATE, -0.1f)) {
        fprintf(stderr, "vorbis_encode_init_vbr failed\n");
        return 1;
    }
    vorbis_comment vc;
    vorbis_comment_init(&vc);
    vorbis_block vb;
    vorbis_dsp_state vd;
    vorbis_analysis_init(&vd, &vi);
    vorbis_block_init(&vd, &vb);

    ogg_stream_state os;
    ogg_stream_init(&os, 0xCA7);

    {
        ogg_packet h, hc, hcode;
        vorbis_analysis_headerout(&vd, &vc, &h, &hc, &hcode);
        ogg_stream_packetin(&os, &h);
        ogg_stream_packetin(&os, &hc);
        ogg_stream_packetin(&os, &hcode);
        ogg_page pg;
        while (ogg_stream_flush(&os, &pg)) {
            fwrite(pg.header, 1, pg.header_len, out);
            fwrite(pg.body, 1, pg.body_len, out);
        }
    }

    int written = 0;
    while (written < total) {
        int chunk = total - written;
        if (chunk > 1024) chunk = 1024;
        float **buf = vorbis_analysis_buffer(&vd, chunk);
        for (int i = 0; i < chunk; ++i)
            buf[0][i] = 0.35f * sinf(2.0f * (float)M_PI * 440.0f *
                                     (float)(written + i) / RATE);
        vorbis_analysis_wrote(&vd, chunk);
        written += chunk;
        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, NULL);
            vorbis_bitrate_addblock(&vb);
            ogg_packet op;
            while (vorbis_bitrate_flushpacket(&vd, &op)) {
                ogg_stream_packetin(&os, &op);
                ogg_page pg;
                while (ogg_stream_pageout(&os, &pg)) {
                    fwrite(pg.header, 1, pg.header_len, out);
                    fwrite(pg.body, 1, pg.body_len, out);
                }
            }
        }
    }
    vorbis_analysis_wrote(&vd, 0);
    while (vorbis_analysis_blockout(&vd, &vb) == 1) {
        vorbis_analysis(&vb, NULL);
        vorbis_bitrate_addblock(&vb);
        ogg_packet op;
        while (vorbis_bitrate_flushpacket(&vd, &op)) {
            ogg_stream_packetin(&os, &op);
            ogg_page pg;
            while (ogg_stream_pageout(&os, &pg)) {
                fwrite(pg.header, 1, pg.header_len, out);
                fwrite(pg.body, 1, pg.body_len, out);
            }
        }
    }
    {
        ogg_page pg;
        while (ogg_stream_flush(&os, &pg)) {
            fwrite(pg.header, 1, pg.header_len, out);
            fwrite(pg.body, 1, pg.body_len, out);
        }
    }

    ogg_stream_clear(&os);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    fclose(out);
    return 0;
}
