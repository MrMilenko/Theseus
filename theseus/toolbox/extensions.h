// extensions.h: header-defined CRT helpers the Xbox runtime is missing.
// Currently just strtok_r (POSIX-style reentrant tokenizer).

#pragma once

char* strtok_r(char* str, const char* delimiters, char** saveptr)
{
    char* token;

    if (str == NULL)
	{
        str = *saveptr;
	}

    if (*str == '\0') 
	{
        *saveptr = str;
        return NULL;
    }

	str += strspn(str, delimiters);

	if (*str == '\0') 
	{
        *saveptr = str;
        return NULL;
    }

	token = str + strcspn(str, delimiters);
	if (*token == '\0')
    {
      *saveptr = token;
      return str;
    }

	*token = '\0';
	*saveptr = token + 1;
	return str;
}

// strnicmp is provided by the XDK (String.h)