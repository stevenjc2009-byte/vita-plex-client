#pragma once
unsigned text_next(const char **text);
unsigned text_count(const char *text);
const char *text_at(const char *text,unsigned index);
int text_append(char *buffer,unsigned cap,const char *character);
void text_delete(char *buffer);
