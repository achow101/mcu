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



#include "keys.h"
#include "sha2.h"
#include "ecdsa.h"
#include "commander.h"
#include "wordlist_electrum.h"
#include "message.h"
#include "base64.h"
#include "random.h"
#include "utils.h"
#include "bip32.h"
#include "bip39.h"
#include "led.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>


extern const uint8_t MEM_PAGE_ERASE[MEM_PAGE_LEN];
extern const uint16_t MEM_PAGE_ERASE_2X[MEM_PAGE_LEN];

#define ELECTRUM_NUM_WORDS  12

static char mnemonic[256]; // longer than max wordlength+1  *  max numwords  +  1 
						   //		for bip32/39_english -> (8+1)*24+1 = 217
						   //		for electrum -> (12+1)*12+1 = 157


static int split_seed(char **seed_words, const char * message)
{
    int i = 0;
    char delim[] = " ,"; 
    static char msg[256];
    memset(msg,0,256);
    memcpy(msg,message,strlen(message));
    seed_words[i] = strtok( msg, delim );
    for( i=0;  seed_words[i]!=NULL; seed_words[++i] = strtok(NULL, delim) ){ }
    return i;
}


char * mnemonic_from_seed_electrum( char * seed_hex )
{
	if( !seed_hex ){
		return NULL;
	}
    if( !memcmp(seed_hex,MEM_PAGE_ERASE,32) ){
       return NULL;
    }
	static char s[9] = {0};
    long unsigned int idx, w1, w2, w3;
	int n = electrum_wordlist_len;
    memset(mnemonic,0,sizeof(mnemonic));
    int i;
    
    for( i=0; i<32; i+=8 ){
        memcpy(s,seed_hex+i,8);
        sscanf(s,"%lx",&idx);
        w1 =  (idx%n);
        w2 = ((idx/n)+w1)%n;
        w3 = ((idx/n/n)+w2)%n;
        strcat(mnemonic,electrum_wordlist[ w1 ]); mnemonic[strlen(mnemonic)] = ' ';
        strcat(mnemonic,electrum_wordlist[ w2 ]); mnemonic[strlen(mnemonic)] = ' ';
        strcat(mnemonic,electrum_wordlist[ w3 ]);
        mnemonic[strlen(mnemonic)] = (i < 32 - 8) ? ' ' : 0;
    }
    return mnemonic; 
}

void master_from_mnemonic_electrum( const char * mnemo, int m_len )
{
    int i;
    uint8_t stretched_seed[64];
    static char seed_hex[33];
    memset(seed_hex,0,sizeof(seed_hex));

    if( mnemo == NULL ){
        uint8_t id_b[16];
        random_bytes(id_b, 16, 1);
        memcpy(seed_hex,uint8_to_hex(id_b,16),32);
        mnemonic_from_seed_electrum( seed_hex );
    }
    else{
        memset(mnemonic,0,sizeof(mnemonic));
		memcpy(mnemonic,mnemo,m_len);
        uint16_t * idx = index_from_mnemonic_electrum( mnemonic ); // offset 1
		if( idx[0]==0 ){
			fill_report("seed","Invalid mnemonic.",ERROR);
			return;
		}
		int64_t index0, index1, index2, index_x;
        char index_c[8+1];
        for( i=0; idx[i]; i+=3 ){
            index0 = idx[i]-1;  // remove offset
            index1 = idx[i+1]-1;
            index2 = idx[i+2]-1;
            if( index0<0 || index1<0 || index2<0 || i>ELECTRUM_NUM_WORDS ){
                fill_report("seed","Invalid mnemonic.",ERROR);
                return;
            }    
            index_x  = index0;
            index_x += (  (index1-index0)<0 ? electrum_wordlist_len + (index1-index0) : (index1-index0) % electrum_wordlist_len  ) * electrum_wordlist_len ;
            index_x += (  (index2-index1)<0 ? electrum_wordlist_len + (index2-index1) : (index2-index1) % electrum_wordlist_len  ) * electrum_wordlist_len * electrum_wordlist_len;
            sprintf(index_c,"%08llx",index_x);
            strncat(seed_hex,index_c,8);
        }
    }
       
    // key stretching
    // electrum:  s = sha256( s || seed_hex )
    memcpy(stretched_seed,seed_hex,32);
    memcpy(stretched_seed+32,seed_hex,32);
	for( i=0; i<100000; i++ ){
        sha256_Raw(stretched_seed, 64, stretched_seed);
		if( (i%4000)==0 ){
			led_toggle();
		}    
    }
    
    uint8_t priv_key_master[32];
	memcpy(priv_key_master, stretched_seed, 32);
  
    if( !memcmp( memory_electrum_master( priv_key_master ), MEM_PAGE_ERASE, 32 ) ||
        !memcmp( memory_electrum_mnemonic( seed_hex ), MEM_PAGE_ERASE, 32 ) ) 
    {    
        fill_report("seed","Problem saving Electrum master key.",ERROR); 
    }
    else
    {
        fill_report("seed","success",SUCCESS);
    }
}



void generate_key_electrum( uint8_t priv_key_child[32], char * key_path, const uint8_t priv_key_master[32] )
{
    
    // z = sha256_x2( n: || change: || public_key_master )
    char tohash[256], index_b[16], change_b[16];
    uint8_t z[32];
    
    uint8_t public_key_master[64];
    ecdsa_get_public_key64(priv_key_master, public_key_master);

    unsigned long idx;
    unsigned long change;
    
    char * pch;
    pch = strtok (key_path," /,m\\");
    sscanf(pch,"%lu",&change); 
    pch = strtok (NULL, " /,m\\");
    sscanf(pch,"%lu",&idx); 
   
    // TEST key gen for large idx and change values
    sprintf(index_b,"%lx:",idx);
    sprintf(change_b,"%lx:",change);
    
    memset(tohash,0,sizeof(tohash));
    strncat(tohash,index_b,strlen(index_b));
    strncat(tohash,change_b,strlen(change_b));
    memcpy(tohash+strlen(tohash),public_key_master,64);
   

    int len = strlen(index_b)+strlen(change_b)+64;
    sha256_Raw((uint8_t *)tohash, len, z); 
	sha256_Raw(z, 32, z);
   
    
    // privat_key_child = (privat_key_master + z) % order
    // public_key_child = (public_key_master + z) % order
    bignum256 bn_z, bn_privkey;
	bn_read_be(z, &bn_z);
	bn_read_be(priv_key_master, &bn_privkey);
    bn_addmod(&bn_privkey, &bn_z, &order256k1);
    bn_write_be(&bn_privkey, priv_key_child);
    
}


static uint16_t * index_from_mnemonic( const char * mnemo, const char ** wordlist )
{
    int i, j, k;
    static uint16_t seed_index[25];
    memset(seed_index,0,sizeof(seed_index));
    char * seed_word[24] = {NULL}; 
    int seed_words_n = split_seed(seed_word, mnemo);
   
    k=0;
    for( i=0; i<seed_words_n; i++ ){
        for( j=0; wordlist[j]; j++ ) {
            if (strcmp(seed_word[i], wordlist[j]) == 0) { // word found
                seed_index[k++] = j+1; // offset of 1
                break;
            }
        }
	}
    return seed_index;
}

uint16_t * index_from_mnemonic_electrum( const char * mnemo )
{
    return index_from_mnemonic( mnemo, electrum_wordlist );
}

uint16_t * index_from_mnemonic_bip32( const char * mnemo )
{
    return index_from_mnemonic( mnemo, mnemonic_wordlist() );
}


char * mnemonic_from_index_bip32( const uint16_t * idx )
{
    if( !memcmp(idx,MEM_PAGE_ERASE_2X,64) )
    {
       return NULL;
    }
    int i;
	const char ** wordlist = mnemonic_wordlist();
    memset(mnemonic,0,sizeof(mnemonic));
    for( i=0; idx[i]; i++ ){
        strcat(mnemonic,wordlist[ idx[i]-1 ]);
        strcat(mnemonic," ");
    }
    return mnemonic;
}


void master_from_mnemonic_bip32( char * mnemo, int m_len, const char * salt, int s_len, int strength )
{
    uint8_t seed[64];
	static uint8_t data[32];
    memset(mnemonic,0,strlen(mnemonic));

    if( mnemo == NULL ){
        if( !strength ){ strength = 256; }
	    if (strength % 32 || strength < 128 || strength > 256) {
            fill_report("seed","Strength must be a multiple of 32 between 128 and 256.",ERROR); 
		    return;
	    }
        random_bytes(data, 32, 1);
	    mnemo = mnemonic_from_data(data, strength / 8);
		memcpy(mnemonic,mnemo,strlen(mnemo));
    } 
	else{
		memcpy(mnemonic,mnemo,m_len);
	}

    
    if( mnemonic_check(mnemonic)==0 ){
        // error report is filled inside mnemonic_check()
        return;
    }

    if( salt == NULL ){
        mnemonic_to_seed(mnemonic, "", seed, 0 ); 
    }else { 
		char s[s_len];
		memcpy(s,salt,s_len);
        mnemonic_to_seed(mnemonic, s, seed, 0 ); 
    } 

    HDNode master;
	hdnode_from_seed(seed, sizeof(seed), &master);
    
    if( !memcmp( memory_bip32_master( master.private_key ), MEM_PAGE_ERASE, 32 )  ||
        !memcmp( memory_bip32_chaincode( master.chain_code ), MEM_PAGE_ERASE, 32) ||
        !memcmp( memory_bip32_mnemonic( index_from_mnemonic_bip32( mnemonic ) ), MEM_PAGE_ERASE_2X, 64 ) )
    {    
        fill_report("seed","Problem saving BIP32 master key.",ERROR); 
    }
    else
    {
        fill_report("seed","success",SUCCESS);
    }
}



void generate_key_bip32( uint8_t priv_key_child[32], char * key_path, const uint8_t priv_key_master[32], const uint8_t chain_code[32] )
{
    HDNode node;
    node.depth = 0;
    node.fingerprint = 0x00000000;
    node.child_num = 0;
    memcpy(node.chain_code,chain_code,32);
    memcpy(node.private_key,priv_key_master,32);
	hdnode_fill_public_key(&node);

    unsigned long idx;
    
    char * pch;
    pch = strtok (key_path," /,m\\");
    while (pch != NULL)
    {
        sscanf(pch,"%lu",&idx); 
        if( pch[strlen(pch)-1] == '\'')
        {
            hdnode_private_ckd_prime(&node, idx); 
        }
        else
        {
            hdnode_private_ckd(&node, idx); 
        }
        memcpy(priv_key_child,node.private_key,32);
        pch = strtok (NULL, " /,m\\");
    } 
}



void report_master_public_key_electrum(void)
{
    uint8_t * priv_key_master = memory_electrum_master(NULL);
    if( !memcmp(priv_key_master,MEM_PAGE_ERASE,32) )
    {
        fill_report("master_public_key","An Electrum master private key is not set.",ERROR);
    }else {
        uint8_t public_key_master[64];
        ecdsa_get_public_key64(priv_key_master, public_key_master);
        fill_report("master_public_key",uint8_to_hex(public_key_master,64),SUCCESS);
    }
}   

void report_master_public_key_bip32(void)
{
    uint8_t * priv_key_master = memory_bip32_master(NULL);
    uint8_t * chain_code = memory_bip32_chaincode(NULL);
    if( !memcmp(priv_key_master,MEM_PAGE_ERASE,32) || !memcmp(chain_code,MEM_PAGE_ERASE,32) )
    {
        fill_report("master_public_key","A bip32 master private key is not set.",ERROR);
    }else {
        uint8_t public_key_master[33];
	    ecdsa_get_public_key33(priv_key_master, public_key_master);
        fill_report("master_public_key",uint8_to_hex(public_key_master,33),SUCCESS);
    }
}   


// TEST for both wallet types
// original trezor code does not double sha256 the utx... electrum and standard protocol double hashes...
// trezor ecdsa has extra step for sig=(r,s) where if s>order/2, then s = -s... electrum does not do this...
static void sign_generic_report( const uint8_t * priv_key, const char * message, int encoding )
{
    if( encoding == ATTR_der_ ){
        uint8_t sig[64]; 
        if( ecdsa_sign_double( priv_key, hex_to_uint8(message), strlen(message)/2, sig ) ) // 0 on success
        {
            fill_report("sign","Could not sign message.",ERROR);
        }
        else{
            uint8_t der[64]; 
            int der_len = ecdsa_sig_to_der(sig, der);
            fill_report("sign",uint8_to_hex(der, der_len), SUCCESS);
        } 
    }
    else if( encoding == ATTR_message_ ){
        int msg_len = strlen(message);     
        uint8_t sig_m[65]; 
        if( sign_message( priv_key, message, msg_len, sig_m ) )
        {
            fill_report("sign","Could not sign message.",ERROR);
        }
        else{
            int b64len;
            char * b64;
            b64 = base64( (char *)sig_m, 65, &b64len );
            fill_report("sign",b64,SUCCESS);
            free(b64);
        } 
    }
    else if( encoding == ATTR_none_ ){
        uint8_t sig[64]; 
        if( ecdsa_sign_double( priv_key, hex_to_uint8(message), strlen(message)/2, sig ) )
        {
            fill_report("sign","Could not sign message.",ERROR);
        }
        else{
            fill_report("sign",uint8_to_hex(sig, 64), SUCCESS);
        }
    }
    else
    {
        fill_report("sign","Invalid encoding method [1].",ERROR);
    }

}


void sign_electrum( const char * message, char * keypath, int encoding )
{
    uint8_t * priv_key_master = memory_electrum_master(NULL);
    if( !memcmp(priv_key_master,MEM_PAGE_ERASE,32) ) 
    {
        fill_report("sign","An Electrum master private key is not set.",ERROR);
    }
    else 
    {
        uint8_t priv_key_child[32];
        generate_key_electrum( priv_key_child, keypath, priv_key_master );
        sign_generic_report( priv_key_child, message, encoding );
    }
}


void sign_bip32( const char * message, char * keypath, int encoding )
{
    uint8_t * priv_key_master = memory_bip32_master(NULL);
    uint8_t * chain_code = memory_bip32_chaincode(NULL);
    if( !memcmp(priv_key_master,MEM_PAGE_ERASE,32) || !memcmp(chain_code,MEM_PAGE_ERASE,32) ) 
    {    
        fill_report("sign","A BIP32 master private key is not set.",ERROR); 
    }
    else 
    {
        uint8_t priv_key_child[32];
        generate_key_bip32( priv_key_child, keypath, priv_key_master, chain_code );
        sign_generic_report( priv_key_child, message, encoding );
    }
}

