/*
 * Copyright (C) 2016 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHIAS
 * RINGWALD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at 
 * contact@bluekitchen-gmbh.com
 *
 */


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btstack.h"

#include "hxcmod.h"
#include "mods/mod.h"

#define NUM_CHANNELS                2
#define A2DP_SAMPLE_RATE            44100
#define BYTES_PER_AUDIO_SAMPLE      (2*NUM_CHANNELS)
#define AUDIO_TIMEOUT_MS            10 
#define TABLE_SIZE_441HZ            100

typedef enum {
    STREAM_SINE = 0,
    STREAM_MOD,
    STREAM_PTS_TEST
} stream_data_source_t;
    
typedef struct {
    uint16_t a2dp_cid;
    uint8_t  local_seid;
    uint8_t  connected;

    uint32_t time_audio_data_sent; // ms
    uint32_t acc_num_missed_samples;
    uint32_t samples_ready;
    btstack_timer_source_t audio_timer;
    uint8_t  streaming;
    int      max_media_payload_size;
    
    uint8_t  sbc_storage[1030];
    uint16_t sbc_storage_count;
    uint8_t  sbc_ready_to_send;
} a2dp_media_sending_context_t;

static  uint8_t media_sbc_codec_capabilities[] = {
    (AVDTP_SBC_44100 << 4) | AVDTP_SBC_STEREO,
    0xFF,//(AVDTP_SBC_BLOCK_LENGTH_16 << 4) | (AVDTP_SBC_SUBBANDS_8 << 2) | AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS,
    2, 53
}; 

static const int16_t sine_int16[] = {
     0,    2057,    4107,    6140,    8149,   10126,   12062,   13952,   15786,   17557,
 19260,   20886,   22431,   23886,   25247,   26509,   27666,   28714,   29648,   30466,
 31163,   31738,   32187,   32509,   32702,   32767,   32702,   32509,   32187,   31738,
 31163,   30466,   29648,   28714,   27666,   26509,   25247,   23886,   22431,   20886,
 19260,   17557,   15786,   13952,   12062,   10126,    8149,    6140,    4107,    2057,
     0,   -2057,   -4107,   -6140,   -8149,  -10126,  -12062,  -13952,  -15786,  -17557,
-19260,  -20886,  -22431,  -23886,  -25247,  -26509,  -27666,  -28714,  -29648,  -30466,
-31163,  -31738,  -32187,  -32509,  -32702,  -32767,  -32702,  -32509,  -32187,  -31738,
-31163,  -30466,  -29648,  -28714,  -27666,  -26509,  -25247,  -23886,  -22431,  -20886,
-19260,  -17557,  -15786,  -13952,  -12062,  -10126,   -8149,   -6140,   -4107,   -2057,
};

static const char * device_name = "A2DP Source Demo 00:00:00:00:00:00";
static btstack_packet_callback_registration_t hci_event_callback_registration;

// mac 2011:    static const char * device_addr_string = "04:0C:CE:E4:85:D3";
// pts:         static const char * device_addr_string = "00:1B:DC:08:0A:A5";
// mac 2013:    static const char * device_addr_string = "84:38:35:65:d1:15";
// phone 2013:  static const char * device_addr_string = "D8:BB:2C:DF:F0:F2";
// minijambox:  
static const char * device_addr_string = "00:21:3C:AC:F7:38";
// head phones: static const char * device_addr_string = "00:18:09:28:50:18";
// bt dongle:   static const char * device_addr_string = "00:15:83:5F:9D:46";
// RT-B6:       static const char * device_addr_string = "00:75:58:FF:C9:7D";

static bd_addr_t device_addr;
static uint8_t sdp_a2dp_source_service_buffer[150];
static uint8_t media_sbc_codec_configuration[4];
static a2dp_media_sending_context_t media_tracker;

static uint16_t avrcp_cid;
static uint8_t  avrcp_connected;

static stream_data_source_t data_source;

static int sine_phase;

static int hxcmod_initialized;
static modcontext mod_context;
static tracker_buffer_state trkbuf;


/* AVRCP Target context START */
static const uint8_t subunit_info[] = {
    0,0,0,0,
    1,1,1,1,
    2,2,2,2,
    3,3,3,3,
    4,4,4,4,
    5,5,5,5,
    6,6,6,6,
    7,7,7,7
};

static uint32_t company_id = 0x112233;
static uint8_t companies_num = 1;
static uint8_t companies[] = {
    0x00, 0x19, 0x58 //BT SIG registered CompanyID
};

static uint8_t events_num = 13;
static uint8_t events[] = {
    AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED,
    AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED,
    AVRCP_NOTIFICATION_EVENT_TRACK_REACHED_END,
    AVRCP_NOTIFICATION_EVENT_TRACK_REACHED_START,
    AVRCP_NOTIFICATION_EVENT_PLAYBACK_POS_CHANGED,
    AVRCP_NOTIFICATION_EVENT_BATT_STATUS_CHANGED,
    AVRCP_NOTIFICATION_EVENT_SYSTEM_STATUS_CHANGED,
    AVRCP_NOTIFICATION_EVENT_PLAYER_APPLICATION_SETTING_CHANGED,
    AVRCP_NOTIFICATION_EVENT_NOW_PLAYING_CONTENT_CHANGED,
    AVRCP_NOTIFICATION_EVENT_AVAILABLE_PLAYERS_CHANGED,
    AVRCP_NOTIFICATION_EVENT_ADDRESSED_PLAYER_CHANGED,
    AVRCP_NOTIFICATION_EVENT_UIDS_CHANGED,
    AVRCP_NOTIFICATION_EVENT_VOLUME_CHANGED
};

typedef struct {
    uint8_t track_id[8];
    uint32_t song_length_ms;
    avrcp_playback_status_t status;
    uint32_t song_position_ms; // 0xFFFFFFFF if not supported
} avrcp_play_status_info_t;

// python -c "print('a'*512)"
static const char title[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";


avrcp_track_t tracks[] = {
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 1, "Sine", "Generated", "AVRCP Demo", "monotone", 12345},
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02}, 2, "Nao-deceased", "Decease", "AVRCP Demo", "vivid", 12345},
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03}, 3, (char *)title, "Decease", "AVRCP Demo", "vivid", 12345},
};
int current_track_index;
avrcp_play_status_info_t play_info;

/* AVRCP Target context END */

static void a2dp_demo_send_media_packet(void){
    int num_bytes_in_frame = btstack_sbc_encoder_sbc_buffer_length();
    int bytes_in_storage = media_tracker.sbc_storage_count;
    uint8_t num_frames = bytes_in_storage / num_bytes_in_frame;
    a2dp_source_stream_send_media_payload(media_tracker.a2dp_cid, media_tracker.local_seid, media_tracker.sbc_storage, bytes_in_storage, num_frames, 0);
    media_tracker.sbc_storage_count = 0;
    media_tracker.sbc_ready_to_send = 0;
}

static void produce_sine_audio(int16_t * pcm_buffer, int num_samples_to_write){
    int count;
    for (count = 0; count < num_samples_to_write ; count++){
        pcm_buffer[count * 2]     = sine_int16[sine_phase];
        pcm_buffer[count * 2 + 1] = sine_int16[sine_phase];
        sine_phase++;
        if (sine_phase >= TABLE_SIZE_441HZ){
            sine_phase -= TABLE_SIZE_441HZ;
        }
    }
}

static void produce_mod_audio(int16_t * pcm_buffer, int num_samples_to_write){
    hxcmod_fillbuffer(&mod_context, (unsigned short *) &pcm_buffer[0], num_samples_to_write, &trkbuf);
}

static void produce_audio(int16_t * pcm_buffer, int num_samples){
    switch (data_source){
        case STREAM_SINE:
            produce_sine_audio(pcm_buffer, num_samples);
            break;
        case STREAM_MOD:
            produce_mod_audio(pcm_buffer, num_samples);
            break;
        default:
            break;
    }    
}

static int a2dp_demo_fill_sbc_audio_buffer(a2dp_media_sending_context_t * context){
    // perform sbc encodin
    int total_num_bytes_read = 0;
    unsigned int num_audio_samples_per_sbc_buffer = btstack_sbc_encoder_num_audio_frames();
    while (context->samples_ready >= num_audio_samples_per_sbc_buffer
        && (context->max_media_payload_size - context->sbc_storage_count) >= btstack_sbc_encoder_sbc_buffer_length()){

        int16_t pcm_frame[256*NUM_CHANNELS];

        produce_audio(pcm_frame, num_audio_samples_per_sbc_buffer);
        btstack_sbc_encoder_process_data(pcm_frame);
        
        uint16_t sbc_frame_size = btstack_sbc_encoder_sbc_buffer_length(); 
        uint8_t * sbc_frame = btstack_sbc_encoder_sbc_buffer();
        
        total_num_bytes_read += num_audio_samples_per_sbc_buffer;
        memcpy(&context->sbc_storage[context->sbc_storage_count], sbc_frame, sbc_frame_size);
        context->sbc_storage_count += sbc_frame_size;
        context->samples_ready -= num_audio_samples_per_sbc_buffer;
    }
    return total_num_bytes_read;
}

static void a2dp_demo_audio_timeout_handler(btstack_timer_source_t * timer){
    a2dp_media_sending_context_t * context = (a2dp_media_sending_context_t *) btstack_run_loop_get_timer_context(timer);
    btstack_run_loop_set_timer(&context->audio_timer, AUDIO_TIMEOUT_MS); 
    btstack_run_loop_add_timer(&context->audio_timer);
    uint32_t now = btstack_run_loop_get_time_ms();

    uint32_t update_period_ms = AUDIO_TIMEOUT_MS;
    if (context->time_audio_data_sent > 0){
        update_period_ms = now - context->time_audio_data_sent;
    } 

    uint32_t num_samples = (update_period_ms * A2DP_SAMPLE_RATE) / 1000;
    context->acc_num_missed_samples += (update_period_ms * A2DP_SAMPLE_RATE) % 1000;
    
    while (context->acc_num_missed_samples >= 1000){
        num_samples++;
        context->acc_num_missed_samples -= 1000;
    }
    context->time_audio_data_sent = now;
    context->samples_ready += num_samples;

    if (context->sbc_ready_to_send) return;

    a2dp_demo_fill_sbc_audio_buffer(context);

    if ((context->sbc_storage_count + btstack_sbc_encoder_sbc_buffer_length()) > context->max_media_payload_size){
        // schedule sending
        context->sbc_ready_to_send = 1;
        a2dp_source_stream_endpoint_request_can_send_now(context->a2dp_cid, context->local_seid);
    }
}

static void a2dp_demo_timer_start(a2dp_media_sending_context_t * context){
    context->max_media_payload_size = a2dp_max_media_payload_size(context->a2dp_cid, context->local_seid);
    context->sbc_storage_count = 0;
    context->sbc_ready_to_send = 0;
    context->streaming = 1;
    btstack_run_loop_remove_timer(&context->audio_timer);
    btstack_run_loop_set_timer_handler(&context->audio_timer, a2dp_demo_audio_timeout_handler);
    btstack_run_loop_set_timer_context(&context->audio_timer, context);
    btstack_run_loop_set_timer(&context->audio_timer, AUDIO_TIMEOUT_MS); 
    btstack_run_loop_add_timer(&context->audio_timer);
}

static void a2dp_demo_timer_stop(a2dp_media_sending_context_t * context){
    context->time_audio_data_sent = 0;
    context->acc_num_missed_samples = 0;
    context->samples_ready = 0;
    context->streaming = 1;
    context->sbc_storage_count = 0;
    context->sbc_ready_to_send = 0;
    btstack_run_loop_remove_timer(&context->audio_timer);
} 

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel);
    UNUSED(size);
    uint8_t status;
    uint8_t local_seid;
    bd_addr_t address;
    uint16_t cid;

    if (packet_type != HCI_EVENT_PACKET) return;

#ifndef HAVE_BTSTACK_STDIN
    if (hci_event_packet_get_type(packet) == BTSTACK_EVENT_STATE){
        if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
        printf("Create AVDTP Source connection to addr %s.\n", bd_addr_to_str(device_addr));
        status = a2dp_source_establish_stream(device_addr, media_tracker.local_seid, &media_tracker.a2dp_cid);
        if (status != ERROR_CODE_SUCCESS){
            printf("Could not perform command, status 0x%2x\n", status);
        }
        return;
    }
#endif

    if (hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) return;
    switch (packet[2]){
        case A2DP_SUBEVENT_INCOMING_CONNECTION_ESTABLISHED:
            // TODO: check incoming cid
            a2dp_subevent_incoming_connection_established_get_bd_addr(packet, address);
            cid = a2dp_subevent_incoming_connection_established_get_a2dp_cid(packet);
            printf("A2DP_SUBEVENT_INCOMING_CONNECTION_ESTABLISHED, cid 0x%02x,  media_tracker.a2dp_cid 0x%02x\n", cid, media_tracker.a2dp_cid);
            if (cid != media_tracker.a2dp_cid) break;
            
            media_tracker.connected = 1;
            printf("A2DP: Incoming connection established: address %s, a2dp cid 0x%02x. Create stream on local seid %d.\n", 
                bd_addr_to_str(address), media_tracker.a2dp_cid, media_tracker.local_seid);
            status = a2dp_source_establish_stream(device_addr, media_tracker.local_seid, &media_tracker.a2dp_cid);
            if (status != ERROR_CODE_SUCCESS){
                printf("Could not perform command, status 0x%2x\n", status);
            }
            break;
        case A2DP_SUBEVENT_STREAM_ESTABLISHED:
            media_tracker.connected = 1;
            a2dp_subevent_stream_established_get_bd_addr(packet, address);
            status = a2dp_subevent_stream_established_get_status(packet);
            if (status){
                printf("A2DP: Stream establishment failed: status 0x%02x.\n", status);
                break;
            }
            local_seid = a2dp_subevent_stream_established_get_local_seid(packet);
            if (local_seid != media_tracker.local_seid){
                printf("A2DP: Stream establishment failed: wrong local seid %d, expected %d.\n", local_seid, media_tracker.local_seid);
                break;    
            }
            media_tracker.a2dp_cid = a2dp_subevent_stream_established_get_a2dp_cid(packet);
            printf("A2DP: Stream established: address %s, a2dp cid 0x%02x, local seid %d, remote seid %d.\n", bd_addr_to_str(address),
                media_tracker.a2dp_cid, media_tracker.local_seid, a2dp_subevent_stream_established_get_remote_seid(packet));
            printf("Start playing mod, cid 0x%02x.\n", media_tracker.a2dp_cid);
            data_source = STREAM_MOD;
            status = a2dp_source_start_stream(media_tracker.a2dp_cid, media_tracker.local_seid);
            break;

        case A2DP_SUBEVENT_STREAM_STARTED:
            play_info.status = AVRCP_PLAYBACK_STATUS_PLAYING;
            avrcp_target_set_now_playing_info(avrcp_cid, &tracks[data_source], sizeof(tracks)/sizeof(avrcp_track_t));
            avrcp_target_set_playback_status(avrcp_cid, AVRCP_PLAYBACK_STATUS_PLAYING);
            a2dp_demo_timer_start(&media_tracker);
            printf("A2DP: Stream started.\n");
            break;

        case A2DP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW:
            a2dp_demo_send_media_packet();
            break;        

        case A2DP_SUBEVENT_STREAM_SUSPENDED:
            play_info.status = AVRCP_PLAYBACK_STATUS_PAUSED;
            avrcp_target_set_playback_status(avrcp_cid, AVRCP_PLAYBACK_STATUS_PAUSED);
            printf("A2DP: Stream paused.\n");
            a2dp_demo_timer_stop(&media_tracker);
            break;

        case A2DP_SUBEVENT_STREAM_RELEASED:
            avrcp_target_set_now_playing_info(avrcp_cid, NULL, sizeof(tracks)/sizeof(avrcp_track_t));
            play_info.status = AVRCP_PLAYBACK_STATUS_STOPPED;
            avrcp_target_set_playback_status(avrcp_cid, AVRCP_PLAYBACK_STATUS_STOPPED);
            printf("A2DP: Stream released.\n");
            a2dp_demo_timer_stop(&media_tracker);
            break;
        case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
            printf("A2DP: Signaling released.\n");
            cid = a2dp_subevent_signaling_connection_released_get_a2dp_cid(packet);
            if (cid == media_tracker.a2dp_cid) {
                media_tracker.connected = 0;
                media_tracker.a2dp_cid = 0;
            }
            break;
        default:
            printf("A2DP: event 0x%02x is not parsed\n", packet[2]);
            break; 
    }
}

static void avrcp_target_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel);
    UNUSED(size);
    bd_addr_t event_addr;
    uint16_t local_cid;
    uint8_t  status = ERROR_CODE_SUCCESS;

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;
    
    switch (packet[2]){
        case AVRCP_SUBEVENT_CONNECTION_ESTABLISHED: {
            local_cid = avrcp_subevent_connection_established_get_avrcp_cid(packet);
            // if (avrcp_cid != 0 && avrcp_cid != local_cid) {
            //     printf("AVRCP: Connection failed, expected 0x%02X l2cap cid, received 0x%02X\n", avrcp_cid, local_cid);
            //     return;
            // }
            // if (avrcp_cid != local_cid) break;
            
            status = avrcp_subevent_connection_established_get_status(packet);
            if (status != ERROR_CODE_SUCCESS){
                printf("AVRCP: Connection failed: status 0x%02x\n", status);
                return;
            }
            avrcp_connected = 1;
            avrcp_cid = local_cid;
            avrcp_subevent_connection_established_get_bd_addr(packet, event_addr);
            printf("AVRCP: connected to %s, avrcp_cid 0x%02x\n", bd_addr_to_str(event_addr), local_cid);
            
            avrcp_target_set_now_playing_info(avrcp_cid, NULL, sizeof(tracks)/sizeof(avrcp_track_t));
            avrcp_target_set_unit_info(avrcp_cid, AVRCP_SUBUNIT_TYPE_AUDIO, company_id);
            avrcp_target_set_subunit_info(avrcp_cid, AVRCP_SUBUNIT_TYPE_AUDIO, (uint8_t *)subunit_info, sizeof(subunit_info));
            return;
        }
        
        case AVRCP_SUBEVENT_EVENT_IDS_QUERY:
            status = avrcp_target_supported_events(avrcp_cid, events_num, events, sizeof(events));
            break;
        case AVRCP_SUBEVENT_COMPANY_IDS_QUERY:
            status = avrcp_target_supported_companies(avrcp_cid, companies_num, companies, sizeof(companies));
            break;
        case AVRCP_SUBEVENT_PLAY_STATUS_QUERY:
            status = avrcp_target_play_status(avrcp_cid, play_info.song_length_ms, play_info.song_position_ms, play_info.status);            
            break;
        // case AVRCP_SUBEVENT_NOW_PLAYING_INFO_QUERY:
        //     status = avrcp_target_now_playing_info(avrcp_cid);
        //     break;
        case AVRCP_SUBEVENT_OPERATION:{
            avrcp_operation_id_t operation_id = avrcp_subevent_operation_get_operation_id(packet);
            if (!media_tracker.connected) break;
            switch (operation_id){
                case AVRCP_OPERATION_ID_PLAY:
                    printf("AVRCP: PLAY\n");
                    status = a2dp_source_start_stream(media_tracker.a2dp_cid, media_tracker.local_seid);
                    break;
                case AVRCP_OPERATION_ID_PAUSE:
                    printf("AVRCP: PAUSE\n");
                    status = a2dp_source_pause_stream(media_tracker.a2dp_cid, media_tracker.local_seid);
                    break;
                case AVRCP_OPERATION_ID_STOP:
                    printf("AVRCP: STOP\n");
                    status = a2dp_source_disconnect(media_tracker.a2dp_cid);
                    break;
                default:
                    return;
            }
            break;
        }
        case AVRCP_SUBEVENT_CONNECTION_RELEASED:
            printf("AVRCP: Channel released: avrcp_cid 0x%02x\n", avrcp_subevent_connection_released_get_avrcp_cid(packet));
            avrcp_cid = 0;
            return;
        default:
            printf("AVRCP: event not parsed %02x\n", packet[2]);
            break;
    }

    if (status != ERROR_CODE_SUCCESS){
        printf("Responding to event 0x%02x failed with status 0x%02x\n", packet[2], status);
    }
}

#ifdef HAVE_BTSTACK_STDIN
static void show_usage(void){
    bd_addr_t      iut_address;
    gap_local_bd_addr(iut_address);
    printf("\n--- Bluetooth A2DP Source/AVRCP Target Demo %s ---\n", bd_addr_to_str(iut_address));
    printf("b      - AVDTP Source create  connection to addr %s\n", device_addr_string);
    printf("B      - AVDTP Source disconnect\n");
    printf("c      - AVRCP Target create connection to addr %s\n", device_addr_string);
    printf("C      - AVRCP Target disconnect\n");
    printf("0      - AVRCP reset now playing info\n");
    
    printf("x      - start streaming sine\n");
    if (hxcmod_initialized){
        printf("z      - start streaming '%s'\n", mod_name);
    }
    printf("p      - pause streaming\n");

    printf("\n--- Bluetooth  AVRCP Target Commands %s ---\n", bd_addr_to_str(iut_address));
    printf("---\n");
}

static void stdin_process(char cmd){
    uint8_t status = ERROR_CODE_SUCCESS;
    switch (cmd){
        case 'b':
            status = a2dp_source_establish_stream(device_addr, media_tracker.local_seid, &media_tracker.a2dp_cid);
            printf(" - Create AVDTP Source connection to addr %s, cid 0x%02x.\n", bd_addr_to_str(device_addr), media_tracker.a2dp_cid);
            break;
        case 'B':
            printf(" - AVDTP Source Disconnect from cid 0x%2x\n", media_tracker.a2dp_cid);
            status = a2dp_source_disconnect(media_tracker.a2dp_cid);
            break;
        case 'c':
            printf(" - Create AVRCP Target connection to addr %s.\n", bd_addr_to_str(device_addr));
            status = avrcp_target_connect(device_addr, &avrcp_cid);
            break;
        case 'C':
            printf(" - AVRCP Target disconnect\n");
            status = avrcp_target_disconnect(avrcp_cid);
            break;

        case '\n':
        case '\r':
            break;

        case 't':
            printf("STREAM_PTS_TEST.\n");
            data_source = STREAM_PTS_TEST;
            avrcp_target_set_now_playing_info(avrcp_cid, &tracks[data_source], sizeof(tracks)/sizeof(avrcp_track_t));
            if (!media_tracker.connected) break;
            status = a2dp_source_start_stream(media_tracker.a2dp_cid, media_tracker.local_seid);
            break;

        case 'x':
            avrcp_target_set_now_playing_info(avrcp_cid, &tracks[data_source], sizeof(tracks)/sizeof(avrcp_track_t));
            printf("Playing sine.\n");
            data_source = STREAM_SINE;
            if (!media_tracker.connected) break;
            status = a2dp_source_start_stream(media_tracker.a2dp_cid, media_tracker.local_seid);
            break;
        case 'z':
            avrcp_target_set_now_playing_info(avrcp_cid, &tracks[data_source], sizeof(tracks)/sizeof(avrcp_track_t));
            printf("Playing mod.\n");
            data_source = STREAM_MOD;
            if (!media_tracker.connected) break;
            status = a2dp_source_start_stream(media_tracker.a2dp_cid, media_tracker.local_seid);
            break;
        case 'p':
            if (!media_tracker.connected) break;
            printf("Pause stream.\n");
            status = a2dp_source_pause_stream(media_tracker.a2dp_cid, media_tracker.local_seid);
            break;
        case '0':
            avrcp_target_set_now_playing_info(avrcp_cid, NULL, sizeof(tracks)/sizeof(avrcp_track_t));
            printf("Reset now playing info\n");
            break;
        default:
            show_usage();
            return;
    }
    if (status != ERROR_CODE_SUCCESS){
        printf("Could not perform command, status 0x%2x\n", status);
    }
}
#endif


int btstack_main(int argc, const char * argv[]);
int btstack_main(int argc, const char * argv[]){
    UNUSED(argc);
    (void)argv;

    // register for HCI events
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    l2cap_init();
    // Initialize AVDTP Source
    a2dp_source_init();
    a2dp_source_register_packet_handler(&packet_handler);

    avdtp_stream_endpoint_t * local_stream_endpoint = a2dp_source_create_stream_endpoint(AVDTP_AUDIO, AVDTP_CODEC_SBC, media_sbc_codec_capabilities, sizeof(media_sbc_codec_capabilities), media_sbc_codec_configuration, sizeof(media_sbc_codec_configuration));
    if (!local_stream_endpoint){
        printf("A2DP source demo: not enough memory to create local stream endpoint\n");
        return 1;
    }
    media_tracker.local_seid = avdtp_local_seid(local_stream_endpoint);

    // Initialize AVRCP Target
    avrcp_target_init();
    avrcp_target_register_packet_handler(&avrcp_target_packet_handler);
    // Initialize SDP 
    sdp_init();
    memset(sdp_a2dp_source_service_buffer, 0, sizeof(sdp_a2dp_source_service_buffer));
    a2dp_source_create_sdp_record(sdp_a2dp_source_service_buffer, 0x10002, 1, NULL, NULL);
    sdp_register_service(sdp_a2dp_source_service_buffer);
    
    gap_set_local_name(device_name);
    gap_discoverable_control(1);
    gap_set_class_of_device(0x200408);
    
    hxcmod_initialized = hxcmod_init(&mod_context);
    if (hxcmod_initialized){
        hxcmod_setcfg(&mod_context, A2DP_SAMPLE_RATE, 16, 1, 1, 1);
        hxcmod_load(&mod_context, (void *) &mod_data, mod_len);
        printf("loaded mod '%s', size %u\n", mod_name, mod_len);
    }
    // parse human readable Bluetooth address
    sscanf_bd_addr(device_addr_string, device_addr);
    
#ifdef HAVE_BTSTACK_STDIN
    btstack_stdin_setup(stdin_process);
#endif
    // turn on!
    hci_power_control(HCI_POWER_ON);
    return 0;
}
