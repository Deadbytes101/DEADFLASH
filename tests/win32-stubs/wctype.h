#ifndef DEADFLASH_WIN32_STUB_WCTYPE_H
#define DEADFLASH_WIN32_STUB_WCTYPE_H

/* Model the Windows wide-character conversion boundary explicitly. */
#define towupper(value) ((wint_t)(unsigned int)(value))

#endif
