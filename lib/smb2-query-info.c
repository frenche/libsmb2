/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
/*
   Copyright (C) 2016 by Ronnie Sahlberg <ronniesahlberg@gmail.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef STDC_HEADERS
#include <stddef.h>
#endif

#include "smb2.h"
#include "libsmb2.h"
#include "libsmb2-private.h"

int smb2_decode_file_basic_information(
        struct smb2_context *smb2,
        struct smb2_file_basic_information *fs,
        struct smb2_iovec *vec)
{
        uint64_t t;

        smb2_get_uint64(vec, 0, &t);
        win_to_timeval(t, &fs->creation_time);

        smb2_get_uint64(vec, 8, &t);
        win_to_timeval(t, &fs->last_access_time);

        smb2_get_uint64(vec, 16, &t);
        win_to_timeval(t, &fs->last_write_time);

        smb2_get_uint64(vec, 24, &t);
        win_to_timeval(t, &fs->change_time);

        smb2_get_uint32(vec, 32, &fs->file_attributes);

        return 0;
}

int smb2_decode_file_standard_information(
        struct smb2_context *smb2,
        struct smb2_file_standard_information *fs,
        struct smb2_iovec *vec)
{
        smb2_get_uint64(vec, 0, &fs->allocation_size);
        smb2_get_uint64(vec, 8, &fs->end_of_file);
        smb2_get_uint32(vec, 16, &fs->number_of_links);
        smb2_get_uint8(vec, 20, &fs->delete_pending);
        smb2_get_uint8(vec, 20, &fs->directory);

        return 0;
}

int smb2_decode_file_all_information(
        struct smb2_context *smb2,
        struct smb2_file_all_information *fs,
        struct smb2_iovec *vec)
{
        struct smb2_iovec v;

        if (vec->len < 40) {
                return -1;
        }
        
        v.buf = vec->buf;
        v.len = 40;
        smb2_decode_file_basic_information(smb2, &fs->basic, &v);

        if (vec->len < 64) {
                return -1;
        }
        
        v.buf = vec->buf + 40;
        v.len = 24;
        smb2_decode_file_standard_information(smb2, &fs->standard, &v);

        smb2_get_uint64(vec, 64, &fs->index_number);
        smb2_get_uint32(vec, 72, &fs->ea_size);
        smb2_get_uint32(vec, 76, &fs->access_flags);
        smb2_get_uint64(vec, 80, &fs->current_byte_offset);
        smb2_get_uint32(vec, 88, &fs->mode);
        smb2_get_uint32(vec, 92, &fs->alignment_requirement);

        //fs->name = ucs2_to_utf8((uint16_t *)&vec->buf[80], name_len / 2);

        return 0;
}

static int
smb2_encode_query_info_request(struct smb2_context *smb2,
                               struct smb2_pdu *pdu,
                               struct smb2_query_info_request *req)
{
        int len;
        char *buf;

        len = SMB2_QUERY_INFO_REQUEST_SIZE & 0xfffffffe;
        buf = malloc(len);
        if (buf == NULL) {
                smb2_set_error(smb2, "Failed to allocate query buffer");
                return -1;
        }
        memset(buf, 0, len);
        
        pdu->out.iov[pdu->out.niov].len = len;
        pdu->out.iov[pdu->out.niov].buf = buf;
        pdu->out.iov[pdu->out.niov].free = free;

        smb2_set_uint16(&pdu->out.iov[pdu->out.niov], 0, req->struct_size);
        smb2_set_uint8(&pdu->out.iov[pdu->out.niov], 2, req->info_type);
        smb2_set_uint8(&pdu->out.iov[pdu->out.niov], 3, req->file_information_class);
        smb2_set_uint32(&pdu->out.iov[pdu->out.niov],4, req->output_buffer_length);
        smb2_set_uint16(&pdu->out.iov[pdu->out.niov],8, req->input_buffer_offset);
        smb2_set_uint32(&pdu->out.iov[pdu->out.niov],12, req->input_buffer_length);
        smb2_set_uint32(&pdu->out.iov[pdu->out.niov],16, req->additional_information);
        smb2_set_uint32(&pdu->out.iov[pdu->out.niov],20, req->flags);
        memcpy(pdu->out.iov[pdu->out.niov].buf + 24, req->file_id,
               SMB2_FD_SIZE);
        pdu->out.niov++;

        if (req->additional_information > 0) {
                smb2_set_error(smb2, "No support for input buffers, yet");
                return -1;
        }

        return 0;
}

static int
smb2_decode_query_info_reply(struct smb2_context *smb2,
                             struct smb2_pdu *pdu,
                             struct smb2_query_info_reply *rep)
{
        smb2_get_uint16(&pdu->in.iov[0], 0, &rep->struct_size);
        smb2_get_uint16(&pdu->in.iov[0], 2, &rep->output_buffer_offset);
        smb2_get_uint32(&pdu->in.iov[0], 4, &rep->output_buffer_length);

        /* Check we have all the data that the reply claims. */
        if (rep->output_buffer_length >
            (pdu->in.iov[0].len -
             (rep->output_buffer_offset - SMB2_HEADER_SIZE))) {
                smb2_set_error(smb2, "Output buffer overflow");
                return -1;
        }
        
        if (rep->output_buffer_length) {
                rep->output_buffer = &pdu->in.iov[0].buf[rep->output_buffer_offset - SMB2_HEADER_SIZE];
        }

        return 0;
}

int smb2_cmd_query_info_async(struct smb2_context *smb2,
                              struct smb2_query_info_request *req,
                              smb2_command_cb cb, void *cb_data)
{
        struct smb2_pdu *pdu;

        pdu = smb2_allocate_pdu(smb2, SMB2_QUERY_INFO, cb, cb_data);
        if (pdu == NULL) {
                return -1;
        }

        if (smb2_encode_query_info_request(smb2, pdu, req)) {
                smb2_free_pdu(smb2, pdu);
                return -1;
        }
        
        if (smb2_queue_pdu(smb2, pdu)) {
                smb2_free_pdu(smb2, pdu);
                return -1;
        }

        return 0;
}

int smb2_process_query_info_reply(struct smb2_context *smb2,
                                  struct smb2_pdu *pdu)
{
        struct smb2_query_info_reply reply;

        smb2_decode_query_info_reply(smb2, pdu, &reply);

        pdu->cb(smb2, pdu->header.status, &reply, pdu->cb_data);

        return 0;
}
