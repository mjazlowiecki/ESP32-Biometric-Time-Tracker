#include "session.h"

void init_users(void) {
    for (int i = 0; i < MAX_USERS; i++) {
        users[i].user_id = -1;
        users[i].total_worked_minutes = 0;
        users[i].breaks_taken_count = 0;
        users[i].total_break_minutes = 0;
    }
}

UserSession* get_user(int id) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].user_id == id) return &users[i];
    }
    return NULL;
}

UserSession* create_user(int id) {
    UserSession* u = get_user(id);
    if (u) return u;

    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].user_id == -1) {
            users[i].user_id = id;
            users[i].status = STATUS_OFFLINE;
            users[i].extensions_used = 0;
            users[i].total_worked_minutes = 0;
            return &users[i];
        }
    }
    return NULL; // brak wolnych slotów
}

void remove_user(int id) {
    UserSession* u = get_user(id);
    if (u) u->user_id = -1;
}

void get_active_stats(int *p, int *u) {
    *p = 0;
    *u = 0;
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].user_id != -1) {
            if (users[i].status == STATUS_WORKING || users[i].status == STATUS_ALERT_WORK) (*p)++;
            if (users[i].status == STATUS_STUDYING) (*u)++;
        }
    }
}