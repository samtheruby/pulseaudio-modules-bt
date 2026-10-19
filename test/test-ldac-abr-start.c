/*
 *  pulseaudio-modules-bt
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Checks the LDAC quality a new stream starts at, and the PCM block size the
 * encoder asks PulseAudio for. libldac is replaced by stubs that record the
 * quality (EQMID) the encoder is opened with.
 */

#include <config.h>

#include <stdio.h>
#include <string.h>

#include <pulse/proplist.h>
#include <pulse/sample.h>

#include <ldacBT.h>
#include <ldacBT_abr.h>

#include "a2dp/a2dp-api.h"
#include "a2dp/ldac_libs.h"

/* A typical LDAC link MTU (2-DH5 packets). */
#define TEST_MTU 679

static bool abr_library_loaded;
static int opened_eqmid;
static int open_count;

static int encoder_handle_storage;
static int abr_handle_storage;

static HANDLE_LDAC_BT stub_get_handle(void) {
    return (HANDLE_LDAC_BT) &encoder_handle_storage;
}

static void stub_handle_noop(HANDLE_LDAC_BT h) {
}

static int stub_get_version(void) {
    return 0;
}

static int stub_handle_query(HANDLE_LDAC_BT h) {
    return 0;
}

static int stub_init_handle_encode(HANDLE_LDAC_BT h, int mtu, int eqmid, int cm, LDACBT_SMPL_FMT_T fmt, int sf) {
    opened_eqmid = eqmid;
    open_count++;
    return 0;
}

static int stub_set_eqmid(HANDLE_LDAC_BT h, int eqmid) {
    return 0;
}

static int stub_alter_eqmid_priority(HANDLE_LDAC_BT h, int priority) {
    return 0;
}

static int stub_encode(HANDLE_LDAC_BT h, void *p_pcm, int *pcm_used, unsigned char *p_stream, int *stream_sz,
                       int *frame_num) {
    return -1;
}

static HANDLE_LDAC_ABR stub_abr_get_handle(void) {
    return (HANDLE_LDAC_ABR) &abr_handle_storage;
}

static void stub_abr_free_handle(HANDLE_LDAC_ABR h) {
}

static int stub_abr_init(HANDLE_LDAC_ABR h, unsigned int interval_ms) {
    return 0;
}

static int stub_abr_set_thresholds(HANDLE_LDAC_ABR h, unsigned int t3, unsigned int t2, unsigned int t1) {
    return 0;
}

static int stub_abr_proc(HANDLE_LDAC_BT h, HANDLE_LDAC_ABR a, unsigned int depth, unsigned int enable) {
    return 0;
}

ldacBT_get_handle_func_t ldacBT_get_handle_func = stub_get_handle;
ldacBT_free_handle_func_t ldacBT_free_handle_func = stub_handle_noop;
ldacBT_close_handle_func_t ldacBT_close_handle_func = stub_handle_noop;
ldacBT_get_version_func_t ldacBT_get_version_func = stub_get_version;
ldacBT_get_sampling_freq_func_t ldacBT_get_sampling_freq_func = stub_handle_query;
ldacBT_get_bitrate_func_t ldacBT_get_bitrate_func = stub_handle_query;
ldacBT_init_handle_encode_func_t ldacBT_init_handle_encode_func = stub_init_handle_encode;
ldacBT_set_eqmid_func_t ldacBT_set_eqmid_func = stub_set_eqmid;
ldacBT_get_eqmid_func_t ldacBT_get_eqmid_func = stub_handle_query;
ldacBT_alter_eqmid_priority_func_t ldacBT_alter_eqmid_priority_func = stub_alter_eqmid_priority;
ldacBT_encode_func_t ldacBT_encode_func = stub_encode;
ldacBT_get_error_code_func_t ldacBT_get_error_code_func = stub_handle_query;

ldac_ABR_get_handle_func_t ldac_ABR_get_handle_func = stub_abr_get_handle;
ldac_ABR_free_handle_func_t ldac_ABR_free_handle_func = stub_abr_free_handle;
ldac_ABR_Init_func_t ldac_ABR_Init_func = stub_abr_init;
ldac_ABR_set_thresholds_func_t ldac_ABR_set_thresholds_func = stub_abr_set_thresholds;
ldac_ABR_Proc_func_t ldac_ABR_Proc_func = stub_abr_proc;

bool ldac_encoder_load() {
    return true;
}

bool is_ldac_abr_loaded() {
    return abr_library_loaded;
}

/* Lives in a2dp_util.c, which needs every codec; LDAC capability selection
 * is not exercised here. */
bool pa_a2dp_select_cap_frequency(uint32_t freq_cap, pa_sample_spec default_sample_spec,
                                  const pa_a2dp_freq_cap_t *freq_cap_table,
                                  size_t n, pa_a2dp_freq_cap_t *result) {
    return false;
}

static void stub_read(const void **read_buf, size_t read_buf_size, void *data) {
}

static void stub_read_free(const void **read_buf, void *data) {
}

struct stream_start {
    int eqmid;
    size_t block_size;
};

/* Opens one 48 kHz stereo LDAC stream the way module-bluez5-device does.
 * ldac_eqmid is the a2dp_config option, or NULL when it is not set. */
static struct stream_start start_stream(bool abr_loaded, const char *ldac_eqmid) {
    const pa_a2dp_source_t *source = pa_a2dp_ldac.a2dp_source;
    pa_sample_spec default_spec = { .format = PA_SAMPLE_S16LE, .rate = 48000, .channels = 2 };
    pa_sample_spec spec;
    a2dp_ldac_t config;
    struct stream_start result;
    void *codec_data = NULL;

    abr_library_loaded = abr_loaded;
    opened_eqmid = -1;
    open_count = 0;

    memset(&config, 0, sizeof(config));
    config.info = A2DP_SET_VENDOR_ID_CODEC_ID(LDAC_VENDOR_ID, LDAC_CODEC_ID);
    config.frequency = LDAC_SAMPLING_FREQ_48000;
    config.channel_mode = LDAC_CHANNEL_MODE_STEREO;

    source->init(stub_read, stub_read_free, &codec_data);
    if (ldac_eqmid) {
        pa_proplist *user_config = pa_proplist_new();
        pa_proplist_sets(user_config, "ldac_eqmid", ldac_eqmid);
        source->update_user_config(user_config, &codec_data);
        pa_proplist_free(user_config);
    }
    source->config_transport(default_spec, &config, sizeof(config), &spec, &codec_data);
    source->get_block_size(TEST_MTU, &result.block_size, &codec_data);
    source->setup_stream(&codec_data);
    result.eqmid = open_count == 1 ? opened_eqmid : -1;
    source->free(&codec_data);

    return result;
}

static int failures;

static void expect_int(const char *what, int actual, int expected) {
    if (actual != expected) {
        printf("FAIL %s: got %d, expected %d\n", what, actual, expected);
        failures++;
    } else
        printf("ok   %s\n", what);
}

/* PCM bytes per block when each RTP packet holds as many LDAC frames of
 * frame_size bytes as fit in TEST_MTU (128 stereo S16 samples per frame). */
static int block_size_for_frame(int frame_size) {
    return (int) ((TEST_MTU - sizeof(struct rtp_header) - sizeof(struct rtp_payload)) / frame_size * 128 * 4);
}

int main(void) {
    struct stream_start s;
    const int hq_block = block_size_for_frame(330);

    /* Adaptive bit rate: start at Standard Quality like Android, and let ABR
     * raise it to High Quality once the link proves steady. */
    s = start_stream(true, NULL);
    expect_int("default with ABR starts at SQ", s.eqmid, LDACBT_EQMID_SQ);
    /* Blocks stay sized for HQ, the best quality ABR can reach, so latency
     * and the ABR queue thresholds are the same as before. */
    expect_int("default with ABR keeps HQ block size", (int) s.block_size, hq_block);

    s = start_stream(true, "auto");
    expect_int("ldac_eqmid=auto starts at SQ", s.eqmid, LDACBT_EQMID_SQ);
    expect_int("ldac_eqmid=auto keeps HQ block size", (int) s.block_size, hq_block);

    s = start_stream(true, "abr");
    expect_int("ldac_eqmid=abr starts at SQ", s.eqmid, LDACBT_EQMID_SQ);

    /* Fixed qualities chosen by the user are kept exactly. */
    s = start_stream(true, "hq");
    expect_int("ldac_eqmid=hq stays HQ", s.eqmid, LDACBT_EQMID_HQ);
    expect_int("ldac_eqmid=hq HQ block size", (int) s.block_size, hq_block);

    s = start_stream(true, "sq");
    expect_int("ldac_eqmid=sq stays SQ", s.eqmid, LDACBT_EQMID_SQ);
    expect_int("ldac_eqmid=sq SQ block size", (int) s.block_size, block_size_for_frame(220));

    s = start_stream(true, "mq");
    expect_int("ldac_eqmid=mq stays MQ", s.eqmid, LDACBT_EQMID_MQ);
    expect_int("ldac_eqmid=mq MQ block size", (int) s.block_size, block_size_for_frame(110));

    /* Without the ABR library nothing can adapt, so the old HQ start stays. */
    s = start_stream(false, NULL);
    expect_int("default without ABR stays HQ", s.eqmid, LDACBT_EQMID_HQ);
    expect_int("default without ABR HQ block size", (int) s.block_size, hq_block);

    s = start_stream(false, "auto");
    expect_int("ldac_eqmid=auto without ABR stays HQ", s.eqmid, LDACBT_EQMID_HQ);

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
