/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 * Read-only primary identity for the single local Wine process. This is not
 * a Windows privilege/impersonation implementation or a Steam credential. */
static int horizon_server_handle_open_token( struct horizon_server_connection *connection,
                                             const unsigned char *message )
{
    const struct {
        struct horizon_server_request_header header;
        unsigned int handle, access, attributes, flags;
    } *request = (const void *)message;
    struct {
        struct horizon_server_reply_header header;
        unsigned int token, pad;
    } reply = {0};
    struct horizon_server_handle_entry *entry = NULL, *target;
    unsigned int access = request->access;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    /* Handle inheritance/protection are not modeled by this server yet. */
    if ((request->flags & ~3u) || request->attributes ||
        (!(request->flags & 1) && (request->flags & 2)))
        reply.header.error = HORIZON_STATUS_INVALID_PARAMETER;
    else if (request->flags & 1)
    {
        if (request->handle != HORIZON_CURRENT_THREAD_HANDLE &&
            (!(target = horizon_server_find_handle_locked( request->handle )) ||
             target->object->type != HORIZON_SERVER_OBJECT_THREAD))
            reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
        else reply.header.error = 0xc000007cu; /* STATUS_NO_TOKEN: no impersonation */
    }
    else
    {
        if (request->handle != HORIZON_CURRENT_PROCESS_HANDLE)
        {
            target = horizon_server_find_handle_locked( request->handle );
            if (!target) reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
            else if (target->object->type != HORIZON_SERVER_OBJECT_PROCESS)
                reply.header.error = HORIZON_STATUS_OBJECT_TYPE_MISMATCH;
            else if (!(target->access & (0x0400u | 0x1000u)))
                reply.header.error = HORIZON_STATUS_ACCESS_DENIED;
        }
        /* TOKEN_READ = READ_CONTROL | TOKEN_QUERY. Mutable/impersonation
         * rights are explicitly unavailable; never report them granted. */
        if (!reply.header.error)
            reply.header.error = horizon_token_read_access( access, &access );
        if (!reply.header.error)
        {
            if (horizon_primary_token)
                entry = horizon_server_create_handle_for_object_locked( horizon_primary_token );
            else if ((entry = horizon_server_create_handle_locked( HORIZON_SERVER_OBJECT_TOKEN )))
                horizon_primary_token = entry->object;
            if (!entry) reply.header.error = HORIZON_STATUS_NO_MEMORY;
            else
            {
                entry->access = access;
                reply.token = entry->handle;
            }
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}
