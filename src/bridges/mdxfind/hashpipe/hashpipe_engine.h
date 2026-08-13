#ifndef HASHPIPE_ENGINE_H
#define HASHPIPE_ENGINE_H

int hp_engine_init(void);
int hp_engine_has_handler(int type_idx);
void *hp_engine_workspace_create(void);
void hp_engine_workspace_destroy(void *workspace);
int hp_engine_verify(void *workspace, int type_idx,
    const char *target, int target_len,
    const unsigned char *pass, int passlen);

#endif
