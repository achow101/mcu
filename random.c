/*

 Copyright (c) 2015 Douglas J. Bakkum

 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the "Software"),
 to deal in the Software without restriction, including without limitation
 the rights to use, copy, modify, merge, publish, distribute, sublicense,
 and/or sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included
 in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
 OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.

*/



#include <string.h>
#include <stdio.h>
#include "random.h"

#ifdef NOT_EMBEDDED

static FILE *f;
void rand_init(void)
{
	f = fopen("/dev/urandom", "r");
}
int random_bytes(uint8_t *buf, uint32_t len, uint8_t update_seed)
{
	(void) update_seed;
    fread(buf, 1, len, f);
    return 0;
}

#else

#include "ataes132.h"

void rand_init(void){};
int random_bytes(uint8_t *buf, uint32_t len, uint8_t update_seed)
{
    // TEST bit order 00000010=0x02 or 01000000=0x40 ?
    const uint8_t ataes_cmd[] = {0x02, 0x02, 0x00, 0x00, 0x00, 0x00}; // pseudo RNG
    const uint8_t ataes_cmd_up[] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00}; // true RNG - writes to EEPROM
    uint8_t ataes_ret[20] = {0}; // size of Random command return packet [Count(1) || Return Code (1) | Data(16) || CRC (2)]
    
    uint32_t cnt = 0;
    while( len > cnt ){
        if( update_seed ){  
            aes_process(ataes_cmd_up, sizeof(ataes_cmd_up), ataes_ret, sizeof(ataes_ret));
            update_seed = 0;
        }else{
            aes_process(ataes_cmd, sizeof(ataes_cmd), ataes_ret, sizeof(ataes_ret));
        }
        if( ataes_ret[0] ){
            // TEST for len > 16 and len%16!=0
            memcpy(buf+cnt,ataes_ret+1, (len-cnt)<16 ? (len-cnt) : 16); 
        }else{
            return 1; // error
        }
        cnt += 16;
    }
    return 0;
}

#endif