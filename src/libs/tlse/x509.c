/*
 *  Copyright (C) 2019 - This file is part of x509-parser project
 *
 *  Author:
 *      Arnaud EBALARD <arnaud.ebalard@ssi.gouv.fr>
 *
 *  BSD License
 *  -----------
 *  
 *  Copyright (C) 2019
 *  All rights reserved.
 *  
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  
 *  1. Redistributions of source code must retain the above copyright notice, this
 *     list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 *  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *  
 */

#ifndef __X509_CONFIG_H__
#define __X509_CONFIG_H__

#define MAX_UINT32 (0xffffffffUL)
#define ASN1_MAX_BUFFER_SIZE (MAX_UINT32)

typedef enum {
	X509_PARSER_ERROR_VERSION_ABSENT            = -1,
	X509_PARSER_ERROR_VERSION_UNEXPECTED_LENGTH = -2,
	X509_PARSER_ERROR_VERSION_NOT_3             = -3,
} x509_parser_errors;

#define TEMPORARY_LAXIST_HANDLE_ALL_REMAINING_RDN_OIDS
#define TEMPORARY_LAXIST_HANDLE_COMMON_UNSUPPORTED_EXT_OIDS
#define TEMPORARY_LAXIST_HANDLE_ALL_REMAINING_EXT_OIDS
#define TEMPORARY_LAXIST_RDN_UPPER_BOUND
#define TEMPORARY_LAXIST_CA_WO_SKI
#define TEMPORARY_LAXIST_EMAILADDRESS_WITH_UTF8_ENCODING
#define TEMPORARY_BAD_EXT_OIDS
#define TEMPORARY_BAD_OID_RDN
#define TEMPORARY_LAXIST_DIRECTORY_STRING
#define TEMPORARY_LAXIST_SERIAL_NEGATIVE
#define TEMPORARY_LAXIST_SERIAL_LENGTH
#define TEMPORARY_LAXIST_SERIAL_NULL
#define TEMPORARY_LAXIST_CA_BASIC_CONSTRAINTS_BOOLEAN_EXPLICIT_FALSE
#define TEMPORARY_LAXIST_EXTENSION_CRITICAL_FLAG_BOOLEAN_EXPLICIT_FALSE
#define TEMPORARY_LAXIST_SKI_CRITICAL_FLAG_SET
#define TEMPORARY_LAXIST_SERIAL_RDN_AS_IA5STRING
#define TEMPORARY_LAXIST_RSA_PUBKEY_AND_SIG_NO_PARAMS_INSTEAD_OF_NULL
#define TEMPORARY_LAXIST_ALLOW_MISSING_CRL_NEXT_UPDATE
#define TEMPORARY_LAXIST_ALLOW_CRL_ENTRY_EXT_WITH_EMPTY_SEQ
#define TEMPORARY_LAXIST_ALLOW_IDP_CRL_EXT_WITHOUT_CRITICAL_BIT_SET
#define TEMPORARY_LAXIST_ALLOW_REVOKED_CERTS_LIST_EMPTY
#define TEMPORARY_LAXIST_ALLOW_MISSING_AKI_OR_CRLNUM

#endif 

#ifndef __X509_UTILS_H__
#define __X509_UTILS_H__

#include <stdint.h>
#include <unistd.h>
#include <string.h>

typedef uint8_t	  x509_u8;
typedef uint16_t x509_u16;
typedef uint32_t x509_u32;
typedef uint64_t x509_u64;

#if defined(__FRAMAC__)
#define ATTRIBUTE_UNUSED
#else
#define ATTRIBUTE_UNUSED __attribute__((unused))
#endif

#ifdef ERROR_TRACE_ENABLE
#define ERROR_TRACE_APPEND(x) do {			    \
	       extern int printf(const char *format, ...);  \
	       printf("%06d ", (x));			    \
	} while (0);
#else
#define ERROR_TRACE_APPEND(x)
#endif

#define X509_FILE_LINE_NUM_ERR ((X509_FILE_NUM * 100000) + __LINE__)

#define P99_PROTECT(...) __VA_ARGS__

int bufs_differ(const x509_u8 *b1, const x509_u8 *b2, x509_u32 n);

#endif 

#ifndef __X509_COMMON_H__
#define __X509_COMMON_H__

typedef enum {
	CLASS_UNIVERSAL        = 0x00,
	CLASS_APPLICATION      = 0x01,
	CLASS_CONTEXT_SPECIFIC = 0x02,
	CLASS_PRIVATE          = 0x03
} tag_class;

typedef enum {
	ASN1_TYPE_BOOLEAN         = 0x01,
	ASN1_TYPE_INTEGER         = 0x02,
	ASN1_TYPE_BIT_STRING      = 0x03,
	ASN1_TYPE_OCTET_STRING    = 0x04,
	ASN1_TYPE_NULL            = 0x05,
	ASN1_TYPE_OID             = 0x06,
	ASN1_TYPE_ENUMERATED      = 0x0a,
	ASN1_TYPE_SEQUENCE        = 0x10,
	ASN1_TYPE_SET             = 0x11,
	ASN1_TYPE_PrintableString = 0x13,
	ASN1_TYPE_T61String       = 0x14,
	ASN1_TYPE_IA5String       = 0x16,
	ASN1_TYPE_UTCTime         = 0x17,
	ASN1_TYPE_GeneralizedTime = 0x18,
} asn1_type;

typedef enum {
	HASH_ALG_UNKNOWN      =  0,
	HASH_ALG_MD2          =  1,
	HASH_ALG_MD4	      =  2,
	HASH_ALG_MD5	      =  3,
	HASH_ALG_MDC2	      =  4,
	HASH_ALG_SHA1	      =  5,
	HASH_ALG_WHIRLPOOL    =  6,
	HASH_ALG_RIPEMD160    =  7,
	HASH_ALG_RIPEMD128    =  8,
	HASH_ALG_RIPEMD256    =  9,
	HASH_ALG_SHA224	      = 10,
	HASH_ALG_SHA256	      = 11,
	HASH_ALG_SHA384	      = 12,
	HASH_ALG_SHA512	      = 13,
	HASH_ALG_SHA512_224   = 14,
	HASH_ALG_SHA512_256   = 15,
	HASH_ALG_SHA3_224     = 16,
	HASH_ALG_SHA3_256     = 17,
	HASH_ALG_SHA3_384     = 18,
	HASH_ALG_SHA3_512     = 19,
	HASH_ALG_SHAKE128     = 20,
	HASH_ALG_SHAKE256     = 21,
	HASH_ALG_SM3          = 22,
	HASH_ALG_GOSTR3411_94 = 23,
	HASH_ALG_STREEBOG256  = 24,
	HASH_ALG_STREEBOG512  = 25,
	HASH_ALG_HBELT        = 26,
	HASH_ALG_BASH256      = 27,
	HASH_ALG_BASH384      = 28,
	HASH_ALG_BASH512      = 29
} hash_alg_id;

typedef enum {
	SIG_ALG_UNKNOWN            =  0,
	SIG_ALG_DSA                =  1,
	SIG_ALG_RSA_SSA_PSS        =  2,
	SIG_ALG_RSA_PKCS1_V1_5     =  3,
	SIG_ALG_ED25519            =  4,
	SIG_ALG_ED448              =  5,
	SIG_ALG_SM2                =  6,
	SIG_ALG_GOSTR3410_2012_256 =  7,
	SIG_ALG_GOSTR3410_2012_512 =  8,
	SIG_ALG_GOSTR3410_2001     =  9,
	SIG_ALG_GOSTR3410_94       = 10,
	SIG_ALG_BIGN               = 11,
	SIG_ALG_ECDSA              = 12,
	SIG_ALG_RSA_9796_2_PAD     = 13,
	SIG_ALG_MONKEYSPHERE       = 14,
	SIG_ALG_BELGIAN_RSA        = 15,
} sig_alg_id;

typedef enum {
	MGF_ALG_UNKNOWN   = 0,
	MGF_ALG_MGF1      = 1  
} mgf_alg_id;

typedef enum {
	SPKI_ALG_UNKNOWN            =  0,
	SPKI_ALG_ECPUBKEY           =  1,
	SPKI_ALG_ED25519            =  2,
	SPKI_ALG_ED448              =  3,
	SPKI_ALG_X25519             =  4,
	SPKI_ALG_X448               =  5,
	SPKI_ALG_RSA                =  6,
	SPKI_ALG_DSA                =  7,
	SPKI_ALG_GOSTR3410_2012_256 =  8, 
	SPKI_ALG_GOSTR3410_2012_512 =  9, 
	SPKI_ALG_GOSTR3410_2001     = 10,
	SPKI_ALG_GOSTR3410_94       = 11,
	SPKI_ALG_BIGN_PUBKEY        = 12,
} spki_alg_id;

typedef enum {
	CURVE_UNKNOWN					=  0,
	CURVE_BIGN256v1					=  1,
	CURVE_BIGN384v1					=  2,
	CURVE_BIGN512v1					=  3,
	CURVE_C2PNB163V1				=  4,
	CURVE_SECT571K1					=  5,
	CURVE_SECT163K1					=  6,
	CURVE_SECP192K1					=  7,
	CURVE_SECP224K1					=  8,
	CURVE_SECP256K1					=  9,
	CURVE_SECP192R1					= 10,
	CURVE_SECP224R1					= 11,
	CURVE_SECP256R1					= 12,
	CURVE_SECP384R1					= 13,
	CURVE_SECP521R1					= 14,
	CURVE_BRAINPOOLP192R1				= 15,
	CURVE_BRAINPOOLP224R1				= 16,
	CURVE_BRAINPOOLP256R1				= 17,
	CURVE_BRAINPOOLP384R1				= 18,
	CURVE_BRAINPOOLP512R1				= 19,
	CURVE_BRAINPOOLP192T1				= 20,
	CURVE_BRAINPOOLP224T1				= 21,
	CURVE_BRAINPOOLP256T1				= 22,
	CURVE_BRAINPOOLP320R1				= 23,
	CURVE_BRAINPOOLP320T1				= 24,
	CURVE_BRAINPOOLP384T1				= 25,
	CURVE_BRAINPOOLP512T1				= 26,
	CURVE_SM2P256TEST				= 27,
	CURVE_SM2P256V1					= 28,
	CURVE_FRP256V1					= 29,
	CURVE_WEI25519					= 30,
	CURVE_WEI448					= 31,
	CURVE_GOST256					= 32,
	CURVE_GOST512					= 33,
	CURVE_GOST_R3410_2012_256_PARAMSETA		= 34,
	CURVE_GOST_R3410_2001_TESTPARAMSET		= 35,
	CURVE_GOST_R3410_2001_CRYPTOPRO_A_PARAMSET	= 36,
	CURVE_GOST_R3410_2001_CRYPTOPRO_B_PARAMSET	= 37,
	CURVE_GOST_R3410_2001_CRYPTOPRO_C_PARAMSET	= 38,
	CURVE_GOST_R3410_2001_CRYPTOPRO_XCHA_PARAMSET	= 39,
	CURVE_GOST_R3410_2001_CRYPTOPRO_XCHB_PARAMSET	= 40,
	CURVE_GOST_R3410_2012_256_PARAMSETB		= 41,
	CURVE_GOST_R3410_2012_256_PARAMSETC		= 42,
	CURVE_GOST_R3410_2012_256_PARAMSETD		= 43,
	CURVE_GOST_R3410_2012_512_PARAMSETTEST		= 44,
	CURVE_GOST_R3410_2012_512_PARAMSETA		= 45,
	CURVE_GOST_R3410_2012_512_PARAMSETB		= 46,
	CURVE_GOST_R3410_2012_512_PARAMSETC		= 47,
} curve_id;

typedef enum {
	GOST94_PARAMS_UNKNOWN         = 0,
	GOST94_PARAMS_TEST            = 1,
	GOST94_PARAMS_CRYPTOPRO_A     = 2,
	GOST94_PARAMS_CRYPTOPRO_B     = 3,
	GOST94_PARAMS_CRYPTOPRO_C     = 4,
	GOST94_PARAMS_CRYPTOPRO_D     = 5,
	GOST94_PARAMS_CRYPTOPRO_XCHA  = 6,
	GOST94_PARAMS_CRYPTOPRO_XCHB  = 7,
	GOST94_PARAMS_CRYPTOPRO_XCHC  = 8
} _gost94_pub_params_id;

typedef struct {
	const x509_u8 *alg_name;
	const x509_u8 *alg_printable_oid;
	const x509_u8 *alg_der_oid;
	const x509_u32 alg_der_oid_len;
	hash_alg_id hash_id;
} _hash_alg;

typedef struct {
	const x509_u8 *alg_name;
	const x509_u8 *alg_printable_oid;
	const x509_u8 *alg_der_oid;
	const x509_u32 alg_der_oid_len;
	const mgf_alg_id mgf_id;
} _mgf;

typedef struct {
	const _hash_alg *hash;
	const _mgf *mgf;
	const _hash_alg *mgf_hash;
	x509_u32 salt_len;
	x509_u32 trailer_field;
} _rsassa_pss;

typedef struct {
	const x509_u8 *crv_name;
	const x509_u8 *crv_printable_oid;
	const x509_u8 *crv_der_oid;
	const x509_u32 crv_der_oid_len;
	const x509_u32 crv_order_bit_len;
	curve_id crv_id;
} _curve;

typedef struct { 
	curve_id curve;
	x509_u32 curve_order_bit_len;

	int compression; 
	x509_u32 ecc_raw_x_off;
	x509_u32 ecc_raw_x_len;
	x509_u32 ecc_raw_y_off; 
	x509_u32 ecc_raw_y_len; 
} spki_ecpubkey_params;

typedef struct { 
	curve_id curve;
	x509_u32 curve_order_bit_len;

	x509_u32 ed25519_raw_pub_off;
	x509_u32 ed25519_raw_pub_len;
} spki_ed25519_params;

typedef struct { 
	curve_id curve;
	x509_u32 curve_order_bit_len;

	x509_u32 ed448_raw_pub_off;
	x509_u32 ed448_raw_pub_len;
} spki_ed448_params;

typedef struct { 
	curve_id curve;
	x509_u32 curve_order_bit_len;

	x509_u32 x25519_raw_pub_off;
	x509_u32 x25519_raw_pub_len;
} spki_x25519_params;

typedef struct { 
	curve_id curve;
	x509_u32 curve_order_bit_len;

	x509_u32 x448_raw_pub_off;
	x509_u32 x448_raw_pub_len;
} spki_x448_params;

typedef struct { 

	x509_u32 rsa_advertised_bit_len;

	x509_u32 rsa_raw_modulus_off; 
	x509_u32 rsa_raw_modulus_len;
	x509_u32 rsa_raw_pub_exp_off; 
	x509_u32 rsa_raw_pub_exp_len;
} spki_rsa_params;

typedef struct { 
	x509_u32 dsa_raw_pub_off; 
	x509_u32 dsa_raw_pub_len;
	x509_u32 dsa_raw_p_off; 
	x509_u32 dsa_raw_p_len;
	x509_u32 dsa_raw_q_off; 
	x509_u32 dsa_raw_q_len;
	x509_u32 dsa_raw_g_off; 
	x509_u32 dsa_raw_g_len;
} spki_dsa_params;

typedef struct { 
	x509_u32 gost94_raw_pub_off;
	x509_u32 gost94_raw_pub_len;
	_gost94_pub_params_id gost94_params_id;
} spki_gost94_params;

typedef struct { 
	curve_id curve; 
	x509_u32 curve_order_bit_len;

	x509_u32 gost2001_raw_x_pub_off; 
	x509_u32 gost2001_raw_x_pub_len; 
	x509_u32 gost2001_raw_y_pub_off; 
	x509_u32 gost2001_raw_y_pub_len; 
} spki_gost2001_params;

typedef struct { 
	curve_id curve; 
	x509_u32 curve_order_bit_len;

	x509_u32 gost2012_256_raw_x_pub_off; 
	x509_u32 gost2012_256_raw_x_pub_len; 
	x509_u32 gost2012_256_raw_y_pub_off; 
	x509_u32 gost2012_256_raw_y_pub_len; 
} spki_gost2012_256_params;

typedef struct { 
	curve_id curve; 
	x509_u32 curve_order_bit_len;

	x509_u32 gost2012_512_raw_x_pub_off; 
	x509_u32 gost2012_512_raw_x_pub_len; 
	x509_u32 gost2012_512_raw_y_pub_off; 
	x509_u32 gost2012_512_raw_y_pub_len; 
} spki_gost2012_512_params;

typedef spki_rsa_params spki_ea_rsa_params; 

typedef struct { 
	curve_id curve;
	x509_u32 curve_order_bit_len;

	x509_u32 bign_raw_x_pub_off;
	x509_u32 bign_raw_x_pub_len;
	x509_u32 bign_raw_y_pub_off;
	x509_u32 bign_raw_y_pub_len;
} spki_bign_params;

typedef struct { 
	x509_u32 avest_raw_pub_off;
	x509_u32 avest_raw_pub_len;
} spki_avest_params;

typedef spki_rsa_params spki_weird_rsa_params; 

typedef union {
	spki_ecpubkey_params	 ecpubkey;     
	spki_ed25519_params	 ed25519;      
	spki_ed448_params	 ed448;	       
	spki_x25519_params	 x25519;       
	spki_x448_params	 x448;	       
	spki_rsa_params		 rsa;	       
	spki_ea_rsa_params	 ea_rsa;       
	spki_dsa_params		 dsa;	       
	spki_gost94_params	 gost94;       
	spki_gost2001_params	 gost2001;     
	spki_gost2012_256_params gost2012_256; 
	spki_gost2012_512_params gost2012_512; 
	spki_bign_params	 bign;	       
	spki_avest_params	 avest;        
} spki_params;

typedef struct { 
	x509_u32 r_raw_off;
	x509_u32 r_raw_len; 
	x509_u32 s_raw_off;
	x509_u32 s_raw_len; 
} sig_dsa_params;

typedef struct { 
	x509_u32 sig_raw_off;
	x509_u32 sig_raw_len;

	mgf_alg_id mgf_alg;
	hash_alg_id mgf_hash_alg;
	x509_u8 salt_len;
	x509_u8 trailer_field;
} sig_rsa_ssa_pss_params;

typedef struct { 
	x509_u32 sig_raw_off;
	x509_u32 sig_raw_len;
} sig_rsa_pkcs1_v1_5_params;

typedef sig_rsa_pkcs1_v1_5_params sig_rsa_9796_2_pad_params; 
typedef sig_rsa_pkcs1_v1_5_params sig_belgian_rsa_params; 

typedef struct { 
	x509_u32 r_raw_off;
	x509_u32 r_raw_len; 
	x509_u32 s_raw_off;
	x509_u32 s_raw_len; 
} sig_ed25519_params;

typedef struct { 
	x509_u32 r_raw_off;
	x509_u32 r_raw_len; 
	x509_u32 s_raw_off;
	x509_u32 s_raw_len; 
} sig_ed448_params;

typedef struct { 
	x509_u32 r_raw_off;
	x509_u32 r_raw_len; 
	x509_u32 s_raw_off;
	x509_u32 s_raw_len; 
} sig_sm2_params;

typedef struct { 
	x509_u32 r_raw_off;
	x509_u32 r_raw_len;  
	x509_u32 s_raw_off;
	x509_u32 s_raw_len;  
} sig_gost_r3410_2012_256_params;

typedef struct { 
	x509_u32 r_raw_off;
	x509_u32 r_raw_len;  
	x509_u32 s_raw_off;
	x509_u32 s_raw_len;  
} sig_gost_r3410_2012_512_params;

typedef struct { 
	x509_u32 r_raw_off;
	x509_u32 r_raw_len;  
	x509_u32 s_raw_off;
	x509_u32 s_raw_len;  
} sig_gost_r3410_2001_params;

typedef struct { 
	x509_u32 r_raw_off;
	x509_u32 r_raw_len; 
	x509_u32 s_raw_off;
	x509_u32 s_raw_len; 
} sig_gost_r3410_94_params;

typedef struct { 
	x509_u32 sig_raw_off;
	x509_u32 sig_raw_len; 
} sig_bign_params;

typedef struct { 
	x509_u32 r_raw_off;
	x509_u32 r_raw_len; 
	x509_u32 s_raw_off;
	x509_u32 s_raw_len; 
} sig_ecdsa_params;

typedef struct { 
	x509_u32 sig_raw_off;
	x509_u32 sig_raw_len;
} sig_monkeysphere_params;

typedef union {
	sig_dsa_params                 dsa;                 
	sig_rsa_ssa_pss_params         rsa_ssa_pss;         
	sig_rsa_pkcs1_v1_5_params      rsa_pkcs1_v1_5;      
	sig_ed25519_params             ed25519;             
	sig_ed448_params               ed448;               
	sig_sm2_params                 sm2;                 
	sig_gost_r3410_2012_256_params gost_r3410_2012_256; 
	sig_gost_r3410_2012_512_params gost_r3410_2012_512; 
	sig_gost_r3410_2001_params     gost_r3410_2001;     
	sig_gost_r3410_94_params       gost_r3410_94;       
	sig_bign_params                bign;                
	sig_ecdsa_params               ecdsa;               
	sig_rsa_9796_2_pad_params      rsa_9796_2_pad;      
	sig_monkeysphere_params        monkeysphere;        
	sig_belgian_rsa_params         belgian_rsa;         
} sig_params;

int parse_sig_ed448(sig_params *params, const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten);
int parse_sig_ed25519(sig_params *params, const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten);
int parse_sig_ecdsa(sig_params *params, const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten);
int parse_sig_sm2(sig_params *params, const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten);
int parse_sig_dsa(sig_params *params, const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten);
int parse_sig_rsa_pkcs1_v15(sig_params *params, const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten);
int parse_sig_rsa_ssa_pss(sig_params *params, const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten);
int parse_sig_rsa_9796_2_pad(sig_params *params, const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten);
int parse_sig_rsa_belgian(sig_params *params, const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten);
int parse_sig_gost94(sig_params *params, const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten);
int parse_sig_gost2001(sig_params *params, const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten);
int parse_sig_gost2012_512(sig_params *params, const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten);
int parse_sig_gost2012_256(sig_params *params, const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten);
int parse_sig_bign(sig_params *params, const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten);
int parse_sig_monkey(sig_params *params, const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten);

int parse_algoid_sig_params_ecdsa_with(sig_params *params, hash_alg_id *hash_alg, const x509_u8 *cert, x509_u32 off, x509_u32 len);
int parse_algoid_sig_params_ecdsa_with_specified(sig_params *params, hash_alg_id *hash_alg, const x509_u8 *cert, x509_u32 off, x509_u32 len);
int parse_algoid_sig_params_sm2(sig_params *params, hash_alg_id *hash_alg, const x509_u8 *cert, x509_u32 off, x509_u32 len);
int parse_algoid_sig_params_eddsa(sig_params *params, hash_alg_id *hash_alg, const x509_u8 *cert, x509_u32 off, x509_u32 len);
int parse_algoid_params_rsa(const x509_u8 *cert, x509_u32 off, x509_u32 len);
int parse_algoid_sig_params_rsa(sig_params *params, hash_alg_id *hash_alg, const x509_u8 *cert, x509_u32 off, x509_u32 len);
int parse_algoid_sig_params_rsassa_pss(sig_params *params, hash_alg_id *hash_alg, const x509_u8 *cert, x509_u32 off, x509_u32 len);
int parse_algoid_sig_params_none(sig_params *params, hash_alg_id *hash_alg, const x509_u8 *cert, x509_u32 off, x509_u32 ATTRIBUTE_UNUSED len);
int parse_algoid_sig_params_bign_with_hspec(sig_params *params, hash_alg_id *hash_alg, const x509_u8 *cert, x509_u32 off, x509_u32 len);
int parse_algoid_params_none(const x509_u8 *cert, x509_u32 off, x509_u32 len);

typedef struct {
	const x509_u8 *alg_name;
	const x509_u8 *alg_printable_oid;
	const x509_u8 *alg_der_oid;
	const x509_u32 alg_der_oid_len;

	sig_alg_id sig_id;
	hash_alg_id hash_id;

	int (*parse_algoid_sig_params)(sig_params *params, hash_alg_id *hash_alg, const x509_u8 *cert, x509_u32 off, x509_u32 len);
	int (*parse_sig)(sig_params *params, const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten);
} _sig_alg;

extern const _sig_alg *known_sig_algs[];
extern const x509_u16 num_known_sig_algs;

extern const _curve *known_curves[];

const _sig_alg * find_sig_alg_by_oid(const x509_u8 *buf, x509_u32 len);
const _hash_alg * find_hash_by_oid(const x509_u8 *buf, x509_u32 len);
const _curve * find_curve_by_oid(const x509_u8 *buf, x509_u32 len);

int get_length(const x509_u8 *buf, x509_u32 len,
	       x509_u32 *adv_len, x509_u32 *eaten);

int parse_id_len(const x509_u8 *buf, x509_u32 len,
		 tag_class exp_class, x509_u32 exp_type,
		 x509_u32 *parsed, x509_u32 *content_len);

int parse_explicit_id_len(const x509_u8 *buf, x509_u32 len,
			  x509_u32 exp_ext_type,
			  tag_class exp_int_class, x509_u32 exp_int_type,
			  x509_u32 *parsed, x509_u32 *data_len);

int parse_null(const x509_u8 *buf, x509_u32 len,
	       x509_u32 *parsed);

int parse_OID(const x509_u8 *buf, x509_u32 len,
	      x509_u32 *parsed);

int parse_integer(const x509_u8 *buf, x509_u32 len,
		  tag_class exp_class, x509_u32 exp_type,
		  x509_u32 *hdr_len, x509_u32 *data_len);

int parse_non_negative_integer(const x509_u8 *buf, x509_u32 len,
			       tag_class exp_class, x509_u32 exp_type,
			       x509_u32 *hdr_len, x509_u32 *data_len);

int parse_boolean(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten);

int parse_generalizedTime(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten,
			  x509_u16 *year, x509_u8 *month, x509_u8 *day,
			  x509_u8 *hour, x509_u8 *min, x509_u8 *sec);

#define NAME_TYPE_rfc822Name     0x81
#define NAME_TYPE_dNSName        0x82
#define NAME_TYPE_URI            0x86
#define NAME_TYPE_iPAddress      0x87
#define NAME_TYPE_registeredID   0x88
#define NAME_TYPE_otherName      0xa0
#define NAME_TYPE_x400Address    0xa3
#define NAME_TYPE_directoryName  0xa4
#define NAME_TYPE_ediPartyName   0xa5

int parse_GeneralName(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten, int *empty);
int parse_SerialNumber(const x509_u8 *cert, x509_u32 off, x509_u32 len,
		       tag_class exp_class, x509_u32 exp_type,
		       x509_u32 *eaten);
int verify_correct_time_use(x509_u8 time_type, x509_u16 yyyy);
int parse_Time(const x509_u8 *buf, x509_u32 len, x509_u8 *t_type, x509_u32 *eaten,
	       x509_u16 *year, x509_u8 *month, x509_u8 *day,
	       x509_u8 *hour, x509_u8 *min, x509_u8 *sec);
int verify_correct_time_use(x509_u8 time_type, x509_u16 yyyy);
int parse_AKICertSerialNumber(const x509_u8 *cert, x509_u32 off, x509_u32 len,
			      tag_class exp_class, x509_u32 exp_type,
			      x509_u32 *eaten);
int parse_crldp_reasons(const x509_u8 *buf, x509_u32 len, x509_u32 exp_type, x509_u32 *eaten);
int parse_DistributionPoint(const x509_u8 *buf, x509_u32 len,
			    int *crldp_has_all_reasons, x509_u32 *eaten);
int parse_AIA(const x509_u8 *cert, x509_u32 off, x509_u32 len, int critical);
int parse_ia5_string(const x509_u8 *buf, x509_u32 len, x509_u32 lb, x509_u32 ub);
int parse_x509_Name(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten, int *empty);
int parse_DisplayText(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten);
int parse_nine_bit_named_bit_list(const x509_u8 *buf, x509_u32 len, x509_u16 *val);
int parse_GeneralName(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten, int *empty);
int parse_GeneralNames(const x509_u8 *buf, x509_u32 len, tag_class exp_class,
		       x509_u32 exp_type, x509_u32 *eaten);

x509_u64 time_components_to_comparable_u64(x509_u16 na_year, x509_u8 na_month, x509_u8 na_day,
				      x509_u8 na_hour, x509_u8 na_min, x509_u8 na_sec);

#endif 

#ifndef __X509_CERT_PARSER_H__
#define __X509_CERT_PARSER_H__

typedef struct {

	x509_u32 tbs_start;
	x509_u32 tbs_len;

	x509_u8 version;

	x509_u32 serial_start;
	x509_u32 serial_len;

	x509_u32 tbs_sig_alg_start;
	x509_u32 tbs_sig_alg_len;
	x509_u32 tbs_sig_alg_oid_start; 
	x509_u32 tbs_sig_alg_oid_len;
	x509_u32 tbs_sig_alg_oid_params_start; 
	x509_u32 tbs_sig_alg_oid_params_len;

	x509_u32 issuer_start;
	x509_u32 issuer_len;

	x509_u64 not_before;
	x509_u64 not_after;

	x509_u32 subject_start;
	x509_u32 subject_len;
	int empty_subject;

	int subject_issuer_identical;

	x509_u32 spki_start;
	x509_u32 spki_len;
	x509_u32 spki_alg_oid_start;
	x509_u32 spki_alg_oid_len;
	x509_u32 spki_alg_oid_params_start;
	x509_u32 spki_alg_oid_params_len;
	x509_u32 spki_pub_key_start;
	x509_u32 spki_pub_key_len;
	spki_alg_id spki_alg;
	spki_params spki_alg_params;

	    int has_ski;
	    x509_u32 ski_start;
	    x509_u32 ski_len;

	    int has_aki;
	    int aki_has_keyIdentifier;
	    x509_u32 aki_keyIdentifier_start;
	    x509_u32 aki_keyIdentifier_len;
	    int aki_has_generalNames_and_serial;
	    x509_u32 aki_generalNames_start;
	    x509_u32 aki_generalNames_len;
	    x509_u32 aki_serial_start;
	    x509_u32 aki_serial_len;

	    int has_san;
	    int san_critical;

	    int bc_critical;
	    int ca_true;
	    int pathLenConstraint_set;

	    int has_keyUsage;
	    int keyCertSign_set;
	    int cRLSign_set;

	    int has_eku;

	    int has_crldp;
	    int one_crldp_has_all_reasons;

	    int has_name_constraints;

	x509_u32 sig_alg_start; 
	x509_u32 sig_alg_len;
	sig_alg_id sig_alg; 
	hash_alg_id hash_alg;
	sig_params sig_alg_params; 

	x509_u32 sig_start;
	x509_u32 sig_len;
} cert_parsing_ctx;

int parse_x509_cert(cert_parsing_ctx *ctx, const x509_u8 *buf, x509_u32 len);

#endif 

#ifndef __X509_CRL_PARSER_H__
#define __X509_CRL_PARSER_H__

typedef struct {

	x509_u32 tbs_start;
	x509_u32 tbs_len;

	x509_u8 version;

	x509_u32 tbs_sig_alg_start;
	x509_u32 tbs_sig_alg_len;
	x509_u32 tbs_sig_alg_oid_start; 
	x509_u32 tbs_sig_alg_oid_len;
	x509_u32 tbs_sig_alg_oid_params_start; 
	x509_u32 tbs_sig_alg_oid_params_len;

	x509_u32 issuer_start;
	x509_u32 issuer_len;

	x509_u64 this_update;
	x509_u64 next_update;

	    int has_aki;
	    int aki_has_keyIdentifier;
	    x509_u32 aki_keyIdentifier_start;
	    x509_u32 aki_keyIdentifier_len;
	    int aki_has_generalNames_and_serial;
	    x509_u32 aki_generalNames_start;
	    x509_u32 aki_generalNames_len;
	    x509_u32 aki_serial_start;
	    x509_u32 aki_serial_len;

	    int has_crldp;
	    int one_crldp_has_all_reasons;

	    int has_crlnumber;
	    x509_u32 crlnumber_start;
	    x509_u32 crlnumber_len;

	    int has_revoked_certs;

	x509_u32 sig_alg_start; 
	x509_u32 sig_alg_len;
	sig_alg_id sig_alg; 
	hash_alg_id hash_alg;
	sig_params sig_alg_params; 

	x509_u32 sig_start;
	x509_u32 sig_len;
} crl_parsing_ctx;

int parse_x509_crl(crl_parsing_ctx *ctx, const x509_u8 *buf, x509_u32 len);

#endif 

#ifndef __X509_PARSER_H__
#define __X509_PARSER_H__

int parse_x509_cert_relaxed(cert_parsing_ctx *ctx, const x509_u8 *buf, x509_u32 len, x509_u32 *eaten);
int parse_x509_crl_relaxed(crl_parsing_ctx *ctx, const x509_u8 *buf, x509_u32 len, x509_u32 *eaten);

#endif 

#define X509_FILE_NUM 2 

static int parse_pubkey_ed448(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_pubkey_x448(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_pubkey_ed25519(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_pubkey_x25519(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_pubkey_ec(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_pubkey_rsa(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_pubkey_gostr3410_94(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_pubkey_gostr3410_2001(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_pubkey_gostr3410_2012_256(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_pubkey_gostr3410_2012_512(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_pubkey_dsa(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_pubkey_bign(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);

static int parse_algoid_pubkey_params_ecPublicKey(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_algoid_pubkey_params_ed25519(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_algoid_pubkey_params_ed448(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_algoid_pubkey_params_x25519(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_algoid_pubkey_params_x448(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_algoid_pubkey_params_rsa(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_algoid_pubkey_params_gost_r3410_2012_256(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_algoid_pubkey_params_gost_r3410_2012_512(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_algoid_pubkey_params_gost_r3410_2001(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_algoid_pubkey_params_gost_r3410_94(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_algoid_pubkey_params_dsa(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
static int parse_algoid_pubkey_params_ea_rsa(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 ATTRIBUTE_UNUSED len);
static int parse_algoid_pubkey_params_none(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 ATTRIBUTE_UNUSED len);
static int parse_algoid_pubkey_params_bign(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 ATTRIBUTE_UNUSED len);

typedef struct {
	const x509_u8 *alg_name;
	const x509_u8 *alg_printable_oid;
	const x509_u8 *alg_der_oid;
	const x509_u32 alg_der_oid_len;

	spki_alg_id pubkey_id;

	int (*parse_algoid_pubkey_params)(cert_parsing_ctx ATTRIBUTE_UNUSED *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
	int (*parse_pubkey)(cert_parsing_ctx ATTRIBUTE_UNUSED *ctx, const x509_u8 *cert, x509_u32 off, x509_u32 len);
} _pubkey_alg;

#define DECL_PUBKEY_ALG(TTalg, XXtype, YYparse_pubkey, ZZparse_algoid, UUname, VVoid, WWoidbuf) \
static const x509_u8 _##TTalg##_pubkey_name[] = UUname;            \
static const x509_u8 _##TTalg##_pubkey_printable_oid[] = VVoid;    \
static const x509_u8 _##TTalg##_pubkey_der_oid[] = WWoidbuf;       \
							      \
static const _pubkey_alg _##TTalg##_pubkey_alg = {            \
	.alg_name = _##TTalg##_pubkey_name,                   \
	.alg_printable_oid = _##TTalg##_pubkey_printable_oid, \
	.alg_der_oid = _##TTalg##_pubkey_der_oid,             \
	.alg_der_oid_len = sizeof(_##TTalg##_pubkey_der_oid), \
	.pubkey_id = (XXtype),				      \
	.parse_pubkey = (YYparse_pubkey),		      \
	.parse_algoid_pubkey_params = (ZZparse_algoid),	      \
}

DECL_PUBKEY_ALG(pkcs1_rsaEncryption         , SPKI_ALG_RSA               , parse_pubkey_rsa               , parse_algoid_pubkey_params_rsa                , "PKCS-1 rsaEncryption"           , "1.2.840.113549.1.1.1"       , P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01 }));
DECL_PUBKEY_ALG(weird_rsa_pub_1             , SPKI_ALG_RSA               , parse_pubkey_rsa               , parse_algoid_pubkey_params_rsa                , "Undocumented RSA pub key oid"   , "1.2.840.887.13.1.1.1"       , P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0x77, 0x0d, 0x01, 0x01, 0x01 }));
DECL_PUBKEY_ALG(weird_rsa_pub_2             , SPKI_ALG_RSA               , parse_pubkey_rsa               , parse_algoid_pubkey_params_rsa                , "another rsa pubkey oid"         , "1.18.840.113549.1.1.1"      , P99_PROTECT({ 0x06, 0x09, 0x3a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01 })); 
DECL_PUBKEY_ALG(rsa_gip_cps                 , SPKI_ALG_RSA               , parse_pubkey_rsa               , parse_algoid_pubkey_params_rsa                , "GIP-CPS"                        , "1.2.250.1.71.2.6.1"         , P99_PROTECT({ 0x06, 0x08, 0x2a, 0x81, 0x7a, 0x01, 0x47, 0x02, 0x06, 0x01 }));
DECL_PUBKEY_ALG(rsassa_pss_shake256         , SPKI_ALG_RSA               , parse_pubkey_rsa               , parse_algoid_pubkey_params_none               , "RSASSA-PSS-SHAKE256"            , "1.3.6.1.5.5.7.6.31"         , P99_PROTECT({ 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x06, 0x1f }));
DECL_PUBKEY_ALG(rsassa_pss_shake128         , SPKI_ALG_RSA               , parse_pubkey_rsa               , parse_algoid_pubkey_params_none               , "RSASSA-PSS-SHAKE128"            , "1.3.6.1.5.5.7.6.30"         , P99_PROTECT({ 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x06, 0x1e }));
DECL_PUBKEY_ALG(ea_rsa                      , SPKI_ALG_RSA               , parse_pubkey_rsa               , parse_algoid_pubkey_params_ea_rsa             , "id-ea-rsa"                      , "2.5.8.1.1"                  , P99_PROTECT({ 0x06, 0x04, 0x55, 0x08, 0x01, 0x01 }));
DECL_PUBKEY_ALG(ecpublickey                 , SPKI_ALG_ECPUBKEY          , parse_pubkey_ec                , parse_algoid_pubkey_params_ecPublicKey        , "ecPublicKey"                    , "1.2.840.10045.2.1"          , P99_PROTECT({ 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d,  0x02, 0x01 }));
DECL_PUBKEY_ALG(dsa_pubkey                  , SPKI_ALG_DSA               , parse_pubkey_dsa               , parse_algoid_pubkey_params_dsa                , "DSA subject public key"         , "1.2.840.10040.4.1"          , P99_PROTECT({ 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x38, 0x04, 0x01 }));
DECL_PUBKEY_ALG(x448                        , SPKI_ALG_X448              , parse_pubkey_x448              , parse_algoid_pubkey_params_x448               , "X448"                           , "1.3.101.111"                , P99_PROTECT({ 0x06, 0x03, 0x2b, 0x65, 0x6f }));
DECL_PUBKEY_ALG(ed448                       , SPKI_ALG_ED448             , parse_pubkey_ed448             , parse_algoid_pubkey_params_ed448              , "Ed448"                          , "1.3.101.113"                , P99_PROTECT({ 0x06, 0x03, 0x2b, 0x65, 0x71 }));
DECL_PUBKEY_ALG(x25519                      , SPKI_ALG_X25519            , parse_pubkey_x25519            , parse_algoid_pubkey_params_x25519             , "X25519"                         , "1.3.101.110"                , P99_PROTECT({ 0x06, 0x03, 0x2b, 0x65, 0x6e }));
DECL_PUBKEY_ALG(ed25519                     , SPKI_ALG_ED25519           , parse_pubkey_ed25519           , parse_algoid_pubkey_params_ed25519            , "Ed25519"                        , "1.3.101.112"                , P99_PROTECT({ 0x06, 0x03, 0x2b, 0x65, 0x70 }));
DECL_PUBKEY_ALG(bign                        , SPKI_ALG_BIGN_PUBKEY       , parse_pubkey_bign              , parse_algoid_pubkey_params_bign               , "bign-pubkey"                    , "1.2.112.0.2.0.34.101.45.2.1", P99_PROTECT({ 0x06, 0x0a, 0x2a, 0x70, 0x00, 0x02, 0x00, 0x22, 0x65, 0x2d, 0x02, 0x01 }));
DECL_PUBKEY_ALG(gost_R3410_94               , SPKI_ALG_GOSTR3410_94      , parse_pubkey_gostr3410_94      , parse_algoid_pubkey_params_gost_r3410_94      , "gostR3410-94 public key"        , "1.2.643.2.2.20"             , P99_PROTECT({ 0x06, 0x06, 0x2a, 0x85, 0x03, 0x02, 0x02, 0x14 }));
DECL_PUBKEY_ALG(gost_R3410_2001             , SPKI_ALG_GOSTR3410_2001    , parse_pubkey_gostr3410_2001    , parse_algoid_pubkey_params_gost_r3410_2001    , "gostR3410-2001 public key"      , "1.2.643.2.2.19"             , P99_PROTECT({ 0x06, 0x06, 0x2a, 0x85, 0x03, 0x02, 0x02, 0x13 }));
DECL_PUBKEY_ALG(gost_R3410_2012_512         , SPKI_ALG_GOSTR3410_2012_512, parse_pubkey_gostr3410_2012_512, parse_algoid_pubkey_params_gost_r3410_2012_512, "gost3410-2012-512 public key"   , "1.2.643.7.1.1.1.2"          , P99_PROTECT({ 0x06, 0x08, 0x2a, 0x85, 0x03, 0x07, 0x01, 0x01, 0x01, 0x02 }));
DECL_PUBKEY_ALG(gost_R3410_2012_256         , SPKI_ALG_GOSTR3410_2012_256, parse_pubkey_gostr3410_2012_256, parse_algoid_pubkey_params_gost_r3410_2012_256, "gost3410-2012-256 public key"   , "1.2.643.7.1.1.1.1"          , P99_PROTECT({ 0x06, 0x08, 0x2a, 0x85, 0x03, 0x07, 0x01, 0x01, 0x01, 0x01 }));

static const _pubkey_alg ATTRIBUTE_UNUSED *known_pubkey_algs[] = {
	&_ecpublickey_pubkey_alg,

	&_pkcs1_rsaEncryption_pubkey_alg,
	&_rsa_gip_cps_pubkey_alg,
	&_rsassa_pss_shake256_pubkey_alg,
	&_rsassa_pss_shake128_pubkey_alg,

	&_ed448_pubkey_alg,
	&_ed25519_pubkey_alg,

	&_x448_pubkey_alg,
	&_x25519_pubkey_alg,

	&_gost_R3410_94_pubkey_alg,
	&_gost_R3410_2001_pubkey_alg,
	&_gost_R3410_2012_512_pubkey_alg,
	&_gost_R3410_2012_256_pubkey_alg,

	&_bign_pubkey_alg,

	&_weird_rsa_pub_1_pubkey_alg,
	&_weird_rsa_pub_2_pubkey_alg,
	&_dsa_pubkey_pubkey_alg,
	&_ea_rsa_pubkey_alg,
};

const x509_u16 num_known_pubkey_algs = (sizeof(known_pubkey_algs) / sizeof(known_pubkey_algs[0]));

typedef struct {
	const x509_u8 *params_name;
	const x509_u8 *params_printable_oid;
	const x509_u8 *params_der_oid;
	const x509_u32 params_der_oid_len;

	_gost94_pub_params_id params_id;
} _gost94_pub_params;

#define DECL_GOST94_PARAMS(EEparams, AAid, BBname, CCoid, DDoidbuf)	    \
static const x509_u8 _##EEparams##_gost_94_params_name[] = BBname;               \
static const x509_u8 _##EEparams##_gost_94_params_printable_oid[] = CCoid;       \
static const x509_u8 _##EEparams##_gost_94_params_der_oid[] = DDoidbuf;          \
									    \
static const _gost94_pub_params _##EEparams##_ParamSet = {                  \
	.params_name = _##EEparams##_gost_94_params_name,                   \
	.params_printable_oid = _##EEparams##_gost_94_params_printable_oid, \
	.params_der_oid = _##EEparams##_gost_94_params_der_oid,             \
	.params_der_oid_len = sizeof(_##EEparams##_gost_94_params_der_oid), \
	.params_id = (AAid),						    \
}

DECL_GOST94_PARAMS(GostR3410_94_Test,           GOST94_PARAMS_TEST	    , "GostR3410_94_TestParamSet",            "1.2.643.2.2.32.0", P99_PROTECT({ 0x06, 0x07, 0x2A, 0x85, 0x03, 0x02, 0x02, 0x20, 0x00 }));
DECL_GOST94_PARAMS(GostR3410_94_CryptoPro_A,    GOST94_PARAMS_CRYPTOPRO_A   , "GostR3410_94_CryptoPro_A_ParamSet",    "1.2.643.2.2.32.2", P99_PROTECT({ 0x06, 0x07, 0x2A, 0x85, 0x03, 0x02, 0x02, 0x20, 0x02 }));
DECL_GOST94_PARAMS(GostR3410_94_CryptoPro_B,    GOST94_PARAMS_CRYPTOPRO_B   , "GostR3410_94_CryptoPro_B_ParamSet",    "1.2.643.2.2.32.3", P99_PROTECT({ 0x06, 0x07, 0x2A, 0x85, 0x03, 0x02, 0x02, 0x20, 0x03 }));
DECL_GOST94_PARAMS(GostR3410_94_CryptoPro_C,    GOST94_PARAMS_CRYPTOPRO_C   , "GostR3410_94_CryptoPro_C_ParamSet",    "1.2.643.2.2.32.4", P99_PROTECT({ 0x06, 0x07, 0x2A, 0x85, 0x03, 0x02, 0x02, 0x20, 0x04 }));
DECL_GOST94_PARAMS(GostR3410_94_CryptoPro_D,    GOST94_PARAMS_CRYPTOPRO_D   , "GostR3410_94_CryptoPro_D_ParamSet",    "1.2.643.2.2.32.5", P99_PROTECT({ 0x06, 0x07, 0x2A, 0x85, 0x03, 0x02, 0x02, 0x20, 0x05 }));
DECL_GOST94_PARAMS(GostR3410_94_CryptoPro_XchA, GOST94_PARAMS_CRYPTOPRO_XCHA, "GostR3410_94_CryptoPro_XchA_ParamSet", "1.2.643.2.2.33.1", P99_PROTECT({ 0x06, 0x07, 0x2A, 0x85, 0x03, 0x02, 0x02, 0x21, 0x01 }));
DECL_GOST94_PARAMS(GostR3410_94_CryptoPro_XchB, GOST94_PARAMS_CRYPTOPRO_XCHB, "GostR3410_94_CryptoPro_XchB_ParamSet", "1.2.643.2.2.33.2", P99_PROTECT({ 0x06, 0x07, 0x2A, 0x85, 0x03, 0x02, 0x02, 0x21, 0x02 }));
DECL_GOST94_PARAMS(GostR3410_94_CryptoPro_XchC, GOST94_PARAMS_CRYPTOPRO_XCHC, "GostR3410_94_CryptoPro_XchC_ParamSet", "1.2.643.2.2.33.3", P99_PROTECT({ 0x06, 0x07, 0x2A, 0x85, 0x03, 0x02, 0x02, 0x21, 0x03 }));

static const _gost94_pub_params ATTRIBUTE_UNUSED *known_gost_94_params[] = {
	&_GostR3410_94_Test_ParamSet,
	&_GostR3410_94_CryptoPro_A_ParamSet,
	&_GostR3410_94_CryptoPro_B_ParamSet,
	&_GostR3410_94_CryptoPro_C_ParamSet,
	&_GostR3410_94_CryptoPro_D_ParamSet,
	&_GostR3410_94_CryptoPro_XchA_ParamSet,
	&_GostR3410_94_CryptoPro_XchB_ParamSet,
	&_GostR3410_94_CryptoPro_XchC_ParamSet,
};

const x509_u16 num_known_gost94_params = (sizeof(known_gost_94_params) / sizeof(known_gost_94_params[0]));

const _gost94_pub_params * find_gost94_params_by_oid(const x509_u8 *buf, x509_u32 len)
{
	const _gost94_pub_params *found = NULL;
	const _gost94_pub_params *cur = NULL;
	x509_u8 k;

	if ((buf == NULL) || (len == 0)) {
		goto out;
	}

	for (k = 0; k < num_known_gost94_params; k++) {
		int ret;

		cur = known_gost_94_params[k];

		if (cur->params_der_oid_len != len) {
			continue;
		}

		ret = !bufs_differ(cur->params_der_oid, buf, cur->params_der_oid_len);
		if (ret) {
			found = cur;
			break;
		}
	}

out:
	return found;
}

const _pubkey_alg * find_pubkey_alg_by_oid(const x509_u8 *buf, x509_u32 len)
{
	const _pubkey_alg *found = NULL;
	const _pubkey_alg *cur = NULL;
	x509_u8 k;

	if ((buf == NULL) || (len == 0)) {
		goto out;
	}

	for (k = 0; k < num_known_pubkey_algs; k++) {
		int ret;

		cur = known_pubkey_algs[k];

		if (cur->alg_der_oid_len != len) {
			continue;
		}

		ret = !bufs_differ(cur->alg_der_oid, buf, cur->alg_der_oid_len);
		if (ret) {
			found = cur;
			break;
		}
	}

out:
	return found;
}

static int parse_x509_cert_Version(const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u8 *version, x509_u32 *eaten)
{
	const x509_u8 *buf = cert + off;
	x509_u32 data_len = 0;
	x509_u32 hdr_len = 0;
	int ret;

	if ((cert == NULL) || (version == NULL) || (len == 0) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_explicit_id_len(buf, len, 0,
				    CLASS_UNIVERSAL, ASN1_TYPE_INTEGER,
				    &hdr_len, &data_len);
	if (ret) {
		ret = X509_PARSER_ERROR_VERSION_ABSENT;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;

	if (data_len != 1) {
		ret = X509_PARSER_ERROR_VERSION_UNEXPECTED_LENGTH;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*version = buf[0];
	*eaten = hdr_len + data_len;

	ret = 0;

out:
	return ret;
}

static int parse_CertSerialNumber(cert_parsing_ctx *ctx,
				  const x509_u8 *cert, x509_u32 off, x509_u32 len,
				  tag_class exp_class, x509_u32 exp_type,
				  x509_u32 *eaten)
{
	int ret;

	if ((cert == NULL) || (len == 0) || (eaten == NULL) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_SerialNumber(cert, off, len, exp_class, exp_type, eaten);
	if (ret) {
	       ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
	       goto out;
	}

	ctx->serial_start = off + 2; 
	ctx->serial_len = *eaten - 2;

out:
	return ret;
}

static int parse_algoid_pubkey_params_ecPublicKey(cert_parsing_ctx *ctx,
						  const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	const x509_u8 *buf = cert + off;
	const _curve *curve = NULL;
	x509_u32 oid_len = 0;
	int ret;

	if ((ctx == NULL) || (cert == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (buf[0] == 0x30) {

		ret = -1;
		goto out;
	}

	ret = parse_OID(buf, len, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (oid_len != len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	curve = find_curve_by_oid(buf, oid_len);
	if (curve == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->spki_alg_params.ecpubkey.curve_order_bit_len = curve->crv_order_bit_len;
	ctx->spki_alg_params.ecpubkey.curve = curve->crv_id;

	ret = 0;

out:
	return ret;
}

static int parse_pubkey_eddsa(const x509_u8 *cert, x509_u32 off, x509_u32 len,
			      x509_u32 exp_pub_len, x509_u32 *raw_pub_off, x509_u32 *raw_pub_len)
{
	x509_u32 remain, hdr_len = 0, data_len = 0;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((cert == NULL) || (len == 0) ||
	    (raw_pub_off == NULL) || (raw_pub_len == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_BIT_STRING,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;

	if (buf[0] != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 1;
	remain = data_len - 1;

	if (remain != exp_pub_len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;
	*raw_pub_off = off + hdr_len + 1;
	*raw_pub_len = exp_pub_len;
out:
	return ret;
}

#define ED25519_PUB_LEN 32

static int parse_pubkey_ed25519(cert_parsing_ctx *ctx,
				     const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	return parse_pubkey_eddsa(cert, off, len, ED25519_PUB_LEN,
				  &ctx->spki_alg_params.ed25519.ed25519_raw_pub_off,
				  &ctx->spki_alg_params.ed25519.ed25519_raw_pub_len);
}

#define X25519_PUB_LEN ED25519_PUB_LEN

static int parse_pubkey_x25519(cert_parsing_ctx *ctx,
				     const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	return parse_pubkey_eddsa(cert, off, len, X25519_PUB_LEN,
				  &ctx->spki_alg_params.x25519.x25519_raw_pub_off,
				  &ctx->spki_alg_params.x25519.x25519_raw_pub_len);
}

#define ED448_PUB_LEN  57

static int parse_pubkey_ed448(cert_parsing_ctx *ctx,
				     const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	return parse_pubkey_eddsa(cert, off, len, ED448_PUB_LEN,
				  &ctx->spki_alg_params.ed448.ed448_raw_pub_off,
				  &ctx->spki_alg_params.ed448.ed448_raw_pub_len);
}

#define X448_PUB_LEN ED448_PUB_LEN

static int parse_pubkey_x448(cert_parsing_ctx *ctx,
				     const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	return parse_pubkey_eddsa(cert, off, len, X448_PUB_LEN,
				  &ctx->spki_alg_params.x448.x448_raw_pub_off,
				  &ctx->spki_alg_params.x448.x448_raw_pub_len);
}

static int parse_pubkey_ec(cert_parsing_ctx *ctx,
			   const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	const x509_u8 *buf = cert + off;
	x509_u32 remain;
	x509_u32 hdr_len = 0;
	x509_u32 data_len = 0;
	x509_u32 order_ceil_len;
	int ret;
	x509_u8 pc;

	if ((ctx == NULL) || (cert == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_BIT_STRING,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;

	if (buf[0] != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 1;
	off += 1;
	remain = data_len - 1;

	if (remain == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	pc = buf[0];

	remain -= 1;
	buf += 1;
	off += 1;

	order_ceil_len = (ctx->spki_alg_params.ecpubkey.curve_order_bit_len + 7) / 8;

	switch (pc) {
	case 0x04: 
		if (remain != (order_ceil_len * 2)) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		ctx->spki_alg_params.ecpubkey.compression = pc;
		ctx->spki_alg_params.ecpubkey.ecc_raw_x_off = off;
		ctx->spki_alg_params.ecpubkey.ecc_raw_x_len = order_ceil_len;
		ctx->spki_alg_params.ecpubkey.ecc_raw_y_off = off + order_ceil_len;
		ctx->spki_alg_params.ecpubkey.ecc_raw_y_len = order_ceil_len;
		break;
	case 0x02: 
	case 0x03: 
		if (remain != order_ceil_len) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		ctx->spki_alg_params.ecpubkey.compression = pc;
		ctx->spki_alg_params.ecpubkey.ecc_raw_x_off = off;
		ctx->spki_alg_params.ecpubkey.ecc_raw_x_len = order_ceil_len;
		ctx->spki_alg_params.ecpubkey.ecc_raw_y_off = 0;
		ctx->spki_alg_params.ecpubkey.ecc_raw_y_len = 0;
		break;
	default: 
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
		break;
	}

	ret = 0;

out:
	return ret;
}

static int _parse_pubkey_gost_on_curves(const x509_u8 *cert, x509_u32 off, x509_u32 len,
					x509_u32 exp_pub_len,
					x509_u32 *raw_x_off, x509_u32 *raw_x_len,
					x509_u32 *raw_y_off, x509_u32 *raw_y_len)
{
	const x509_u8 *buf = cert + off;
	x509_u32 remain;
	x509_u32 hdr_len = 0;
	x509_u32 data_len = 0;
	int ret;

	if ((cert == NULL) || (len == 0) ||
	    (raw_x_off == NULL) || (raw_x_len == NULL) ||
	    (raw_y_off == NULL) || (raw_y_len == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_BIT_STRING,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	off += hdr_len;
	buf += hdr_len;

	if (buf[0] != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 1;
	off += 1;
	remain = data_len - 1;

	ret = parse_id_len(buf, remain, CLASS_UNIVERSAL, ASN1_TYPE_OCTET_STRING,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (data_len != exp_pub_len) {
		ret = -1;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;

	}

	buf += hdr_len;
	off += hdr_len;
	remain -= hdr_len;

	*raw_x_off = off;
	*raw_x_len = exp_pub_len / 2;
	*raw_y_off = off + *raw_x_len;
	*raw_y_len = exp_pub_len / 2;

	if (remain != data_len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

#define GOST94_PUB_LEN 128 

static int parse_pubkey_gostr3410_94(cert_parsing_ctx *ctx,
				     const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	const x509_u8 *buf = cert + off;
	x509_u32 remain;
	x509_u32 hdr_len = 0;
	x509_u32 data_len = 0;
	int ret;

	if ((ctx == NULL) || (cert == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_BIT_STRING,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	off += hdr_len;
	buf += hdr_len;

	if (buf[0] != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 1;
	off += 1;
	remain = data_len - 1;

	ret = parse_id_len(buf, remain, CLASS_UNIVERSAL, ASN1_TYPE_OCTET_STRING,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (data_len != GOST94_PUB_LEN) {
		ret = -1;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;

	}

	buf += hdr_len;
	off += hdr_len;
	remain -= hdr_len;

	if (remain != data_len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->spki_alg_params.gost94.gost94_raw_pub_off = off;
	ctx->spki_alg_params.gost94.gost94_raw_pub_len = GOST94_PUB_LEN;

	ret = 0;

out:
	return ret;
}

static inline int parse_pubkey_gostr3410_2001(cert_parsing_ctx *ctx,
				       const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	return _parse_pubkey_gost_on_curves(cert, off, len, 64,
				 &ctx->spki_alg_params.gost2001.gost2001_raw_x_pub_off,
				 &ctx->spki_alg_params.gost2001.gost2001_raw_x_pub_len,
				 &ctx->spki_alg_params.gost2001.gost2001_raw_y_pub_off,
				 &ctx->spki_alg_params.gost2001.gost2001_raw_y_pub_len);
}

static inline int parse_pubkey_gostr3410_2012_256(cert_parsing_ctx *ctx,
					   const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	return _parse_pubkey_gost_on_curves(cert, off, len, 64,
				 &ctx->spki_alg_params.gost2012_256.gost2012_256_raw_x_pub_off,
				 &ctx->spki_alg_params.gost2012_256.gost2012_256_raw_x_pub_len,
				 &ctx->spki_alg_params.gost2012_256.gost2012_256_raw_y_pub_off,
				 &ctx->spki_alg_params.gost2012_256.gost2012_256_raw_y_pub_len);
}

static inline int parse_pubkey_gostr3410_2012_512(cert_parsing_ctx *ctx,
				    const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	return _parse_pubkey_gost_on_curves(cert, off, len, 128,
				 &ctx->spki_alg_params.gost2012_512.gost2012_512_raw_x_pub_off,
				 &ctx->spki_alg_params.gost2012_512.gost2012_512_raw_x_pub_len,
				 &ctx->spki_alg_params.gost2012_512.gost2012_512_raw_y_pub_off,
				 &ctx->spki_alg_params.gost2012_512.gost2012_512_raw_y_pub_len);
}

static int parse_pubkey_bign(cert_parsing_ctx *ctx,
			     const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	x509_u32 order_ceil_len, remain, hdr_len = 0, data_len = 0;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_BIT_STRING,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;

	if (buf[0] != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 1;
	off += 1;
	remain = data_len - 1;

	order_ceil_len = (ctx->spki_alg_params.bign.curve_order_bit_len + 7) / 8;
	if (remain != (2 * order_ceil_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->spki_alg_params.bign.bign_raw_x_pub_off = off;
	ctx->spki_alg_params.bign.bign_raw_x_pub_len = order_ceil_len;
	ctx->spki_alg_params.bign.bign_raw_y_pub_off = off + order_ceil_len;
	ctx->spki_alg_params.bign.bign_raw_y_pub_len = order_ceil_len;

	ret = 0;

out:
	return ret;
}

static int parse_algoid_pubkey_params_bign(cert_parsing_ctx *ctx,
					   const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	const x509_u8 *buf = cert + off;
	const _curve *curve;
	x509_u32 oid_len = 0;
	x509_u32 remain;
	int ret;

	if ((ctx == NULL) || (cert == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = len;
	ret = parse_OID(buf, remain, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	curve = find_curve_by_oid(buf, oid_len);
	if (curve == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += oid_len;
	remain -= oid_len;

	if (remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->spki_alg_params.bign.curve_order_bit_len = curve->crv_order_bit_len;
	ctx->spki_alg_params.bign.curve = curve->crv_id;

	ret = 0;

out:
	return ret;
}

static int parse_algoid_pubkey_params_ed448(cert_parsing_ctx *ctx,
					    const x509_u8 *cert, x509_u32 ATTRIBUTE_UNUSED off, x509_u32 len)
{
	int ret;

	if ((ctx == NULL) || (cert == NULL) || (len != 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->spki_alg_params.ed448.curve = CURVE_WEI448;
	ctx->spki_alg_params.ed448.curve_order_bit_len = 448;

	ret = 0;

out:
	return ret;
}

static int parse_algoid_pubkey_params_x448(cert_parsing_ctx *ctx,
					      const x509_u8 *cert, x509_u32 ATTRIBUTE_UNUSED off, x509_u32 len)
{
	int ret;

	if ((ctx == NULL) || (cert == NULL) || (len != 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->spki_alg_params.x448.curve = CURVE_WEI448;
	ctx->spki_alg_params.x448.curve_order_bit_len = 448;

	ret = 0;

out:
	return ret;
}

static int parse_algoid_pubkey_params_ed25519(cert_parsing_ctx *ctx,
					      const x509_u8 *cert, x509_u32 ATTRIBUTE_UNUSED off, x509_u32 len)
{
	int ret;

	if ((ctx == NULL) || (cert == NULL) || (len != 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->spki_alg_params.ed25519.curve = CURVE_WEI25519;
	ctx->spki_alg_params.ed25519.curve_order_bit_len = 256;

	ret = 0;

out:
	return ret;
}

static int parse_algoid_pubkey_params_x25519(cert_parsing_ctx *ctx,
					      const x509_u8 *cert, x509_u32 ATTRIBUTE_UNUSED off, x509_u32 len)
{
	int ret;

	if ((ctx == NULL) || (cert == NULL) || (len != 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->spki_alg_params.x25519.curve = CURVE_WEI25519;
	ctx->spki_alg_params.x25519.curve_order_bit_len = 256;

	ret = 0;

out:
	return ret;
}

static int parse_algoid_params_gost2012PublicKey(const x509_u8 *cert, x509_u32 off, x509_u32 len,
						 const _curve **curve, const _hash_alg **hash)
{
	x509_u32 remain, hdr_len = 0, data_len = 0;
	const x509_u8 *buf = cert + off;
	x509_u32 oid_len = 0;
	int ret;

	if ((cert == NULL) || (len == 0) || (curve == NULL) || (hash == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	remain = data_len;

	ret = parse_OID(buf, remain, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*curve = find_curve_by_oid(buf, oid_len);
	if (*curve == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += oid_len;
	remain -= oid_len;

	if (remain)  {

		ret = parse_OID(buf, remain, &oid_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		*hash = find_hash_by_oid(buf, oid_len);
		if (*hash == NULL) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += oid_len;
		remain -= oid_len;

		if (remain) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
	}

	ret = 0;

out:
	return ret;
}

static int parse_algoid_pubkey_params_ea_rsa(cert_parsing_ctx *ctx,
					     const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	x509_u32 hdr_len = 0, data_len = 0;
	const x509_u8 *buf = cert + off;
	x509_u32 bit_len = 0, parsed = 0;
	int ret;

	if ((ctx == NULL) || (cert == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	switch (len) {
	case 0: 
		ret = 0;
		break;

	case 2: 
		ret = parse_null(buf, len, &parsed);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
		break;

	case 4: 

		ret = parse_non_negative_integer(buf, len, CLASS_UNIVERSAL,
					     ASN1_TYPE_INTEGER,
					     &hdr_len, &data_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
		bit_len = (((x509_u32)buf[2]) * (1 << 8)) + buf[3];

		break;

	default:
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		break;
	}

	ctx->spki_alg_params.rsa.rsa_advertised_bit_len = bit_len;

out:
	return ret;
}

static int parse_algoid_pubkey_params_gost_r3410_94(cert_parsing_ctx *ctx,
						   const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	x509_u32 remain, hdr_len = 0, data_len = 0, oid_len = 0;
	const _gost94_pub_params *params;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((ctx == NULL) || (cert == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	remain = data_len;

	ret = parse_OID(buf, remain, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	params = find_gost94_params_by_oid(buf, oid_len);
	if (params == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	ctx->spki_alg_params.gost94.gost94_params_id = params->params_id;

	buf += oid_len;
	remain -= oid_len;

	ret = parse_OID(buf, remain, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += oid_len;
	remain -= oid_len;

	if (remain) {

		ret = parse_OID(buf, remain, &oid_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		remain -= oid_len;

		if (remain) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
	}

	ret = 0;

out:
	return ret;

}

static int parse_algoid_pubkey_params_gost_r3410_2001(cert_parsing_ctx *ctx,
						      const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	const x509_u8 *buf = cert + off;
	x509_u32 remain, hdr_len = 0, data_len = 0;
	const _curve *curve;
	const _hash_alg *h;
	x509_u32 parsed = 0;
	x509_u32 oid_len = 0;
	int ret;

	if ((ctx == NULL) || (cert == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->spki_alg_params.gost2001.curve_order_bit_len = 0;
	ctx->spki_alg_params.gost2001.curve = CURVE_UNKNOWN;

	if (len == 0) { 
		ret = 0;
		goto out;
	}

	if (len == 2 && !parse_null(buf, len, &parsed)) { 
		ret = 0;
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	remain = data_len;

	ret = parse_OID(buf, remain, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	curve = find_curve_by_oid(buf, oid_len);
	if (curve == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += oid_len;
	remain -= oid_len;

	ret = parse_OID(buf, remain, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	h = find_hash_by_oid(buf, oid_len);
	if (h == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += oid_len;
	remain -= oid_len;

	if (remain)  {

		ret = parse_OID(buf, remain, &oid_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += oid_len;
		remain -= oid_len;

		if (remain) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
	}

	ctx->spki_alg_params.gost2001.curve_order_bit_len = curve->crv_order_bit_len;

	ctx->spki_alg_params.gost2001.curve = curve->crv_id;

	ret = 0;

out:
	return ret;}

static int parse_algoid_pubkey_params_gost_r3410_2012_256(cert_parsing_ctx *ctx,
							  const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	const _hash_alg *hash = NULL;
	const _curve *curve = NULL;
	int ret;

	if ((ctx == NULL) || (cert == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_algoid_params_gost2012PublicKey(cert, off, len, &curve, &hash);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (curve == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->spki_alg_params.gost2012_256.curve_order_bit_len = curve->crv_order_bit_len;
	ctx->spki_alg_params.gost2012_256.curve = curve->crv_id;

out:

	return ret;
}

static int parse_algoid_pubkey_params_gost_r3410_2012_512(cert_parsing_ctx *ctx,
							  const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	const _hash_alg *hash = NULL;
	const _curve *curve = NULL;
	int ret;

	if (ctx == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_algoid_params_gost2012PublicKey(cert, off, len, &curve, &hash);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (curve == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->spki_alg_params.gost2012_512.curve_order_bit_len = curve->crv_order_bit_len;
	ctx->spki_alg_params.gost2012_256.curve = curve->crv_id;

out:
	return ret;
}

static int parse_algoid_pubkey_params_none(cert_parsing_ctx *ctx,
					   const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	int ret;

	if (ctx == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_algoid_params_none(cert, off, len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

out:
	return ret;
}

static int parse_algoid_pubkey_params_rsa(cert_parsing_ctx *ctx,
					const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	int ret;

	if ((ctx == NULL) || (cert == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_algoid_params_rsa(cert, off, len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

out:
	return ret;
}

static int spki_rsa_export_n_e(const x509_u8 *buf, x509_u32 len,
			       x509_u32 *n_start_off, x509_u32 *n_len,
			       x509_u32 *e_start_off, x509_u32 *e_len)
{
	x509_u32 remain;
	x509_u32 hdr_len = 0;
	x509_u32 data_len = 0;
	x509_u32 parsed = 0;
	x509_u32 off;
	int ret;

	if ((buf == NULL) || (len == 0) ||
	    (n_start_off == NULL) || (n_len == NULL) ||
	    (e_start_off == NULL) || (e_len == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_BIT_STRING,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off = hdr_len;

	if (buf[0] != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 1;
	remain = data_len - 1;
	off += 1;

	ret = parse_id_len(buf, remain, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	remain = data_len;
	off += hdr_len;

	ret = parse_non_negative_integer(buf, remain, CLASS_UNIVERSAL, ASN1_TYPE_INTEGER,
				     &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	parsed = hdr_len + data_len;

	*n_start_off = off + hdr_len;
	*n_len = data_len;

	if ((data_len != 0) && (buf[hdr_len] == 0)) {
		*n_start_off += 1;
		*n_len -= 1;
	}

	buf += parsed;
	off += parsed;
	remain -= parsed;

	ret = parse_non_negative_integer(buf, remain, CLASS_UNIVERSAL, ASN1_TYPE_INTEGER,
				     &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	parsed = hdr_len + data_len;

	*e_start_off = off + hdr_len;
	*e_len = data_len;

	buf += parsed;
	off += parsed;
	remain -= parsed;

	if (remain != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

static int parse_pubkey_rsa(cert_parsing_ctx *ctx,
			    const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	x509_u32 n_start_off = 0, n_len = 0, e_start_off= 0, e_len = 0;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((ctx == NULL) || (cert == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = spki_rsa_export_n_e(buf, len, &n_start_off, &n_len, &e_start_off, &e_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->spki_alg_params.rsa.rsa_raw_modulus_off = off + n_start_off;
	ctx->spki_alg_params.rsa.rsa_raw_modulus_len = n_len;
	ctx->spki_alg_params.rsa.rsa_raw_pub_exp_off = off + e_start_off;
	ctx->spki_alg_params.rsa.rsa_raw_pub_exp_len = e_len;

out:
	return ret;
}

int parse_algoid_dsa_export_params(const x509_u8 *buf, x509_u32 len,
				   x509_u32 *p_start_off, x509_u32 *p_len,
				   x509_u32 *q_start_off, x509_u32 *q_len,
				   x509_u32 *g_start_off, x509_u32 *g_len)
{
	x509_u32 remain = 0;
	x509_u32 hdr_len = 0;
	x509_u32 data_len = 0;
	x509_u32 eaten = 0;
	x509_u32 parsed = 0;
	x509_u32 off = 0;
	int ret;

	if ((buf == NULL) ||
	    (p_start_off == NULL) || (p_len == NULL) ||
	    (q_start_off == NULL) || (q_len == NULL) ||
	    (g_start_off == NULL) || (g_len == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*p_start_off = 0;
	*p_len = 0;
	*q_start_off = 0;
	*q_len = 0;
	*g_start_off = 0;
	*g_len = 0;

	if (len == 0) {
		ret = 0;
		goto out;
	}

	ret = parse_null(buf, len, &parsed);
	if (!ret) {
		ret = 0;
		goto out;
	}

	remain = len;
	ret = parse_id_len(buf, remain, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;
	remain = data_len;

	ret = parse_non_negative_integer(buf, remain, CLASS_UNIVERSAL,
					 ASN1_TYPE_INTEGER,
					 &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	eaten = hdr_len + data_len;

	*p_start_off = off + hdr_len;
	*p_len = data_len;

	remain -= eaten;
	buf += eaten;
	off += eaten;

	ret = parse_non_negative_integer(buf, remain, CLASS_UNIVERSAL,
					 ASN1_TYPE_INTEGER,
					 &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	eaten = hdr_len + data_len;

	*q_start_off = off + hdr_len;
	*q_len = data_len;

	remain -= eaten;
	buf += eaten;
	off += eaten;

	ret = parse_non_negative_integer(buf, remain, CLASS_UNIVERSAL,
					 ASN1_TYPE_INTEGER,
					 &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	eaten = hdr_len + data_len;

	*g_start_off = off + hdr_len;
	*g_len = data_len;
	remain -= eaten;

	if (remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

static int parse_algoid_pubkey_params_dsa(cert_parsing_ctx *ctx,
					  const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	const x509_u8 *buf = cert + off;
	x509_u32 p_start_off, p_len;
	x509_u32 q_start_off, q_len;
	x509_u32 g_start_off, g_len;
	int ret;

	if ((ctx == NULL) || (cert == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_algoid_dsa_export_params(buf, len,
					     &p_start_off, &p_len,
					     &q_start_off, &q_len,
					     &g_start_off, &g_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->spki_alg_params.dsa.dsa_raw_p_off = off + p_start_off;
	ctx->spki_alg_params.dsa.dsa_raw_p_len = p_len;
	ctx->spki_alg_params.dsa.dsa_raw_q_off = off + q_start_off;
	ctx->spki_alg_params.dsa.dsa_raw_q_len = q_len;
	ctx->spki_alg_params.dsa.dsa_raw_g_off = off + g_start_off;
	ctx->spki_alg_params.dsa.dsa_raw_g_len = g_len;

out:
	return ret;
}

int parse_pubkey_dsa_export_pub(const x509_u8 *buf, x509_u32 len,
				x509_u32 *pub_start_off, x509_u32 *pub_len)
{
	x509_u32 remain;
	x509_u32 hdr_len = 0;
	x509_u32 data_len = 0;
	x509_u32 off;
	int ret;

	if ((buf == NULL) || (len == 0) ||
	    (pub_start_off == NULL) || (pub_len == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_BIT_STRING,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off = hdr_len;

	if (buf[0] != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 1;
	remain = data_len - 1;
	off += 1;

	ret = parse_non_negative_integer(buf, remain, CLASS_UNIVERSAL,
					 ASN1_TYPE_INTEGER,
					 &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	remain = data_len;
	off += hdr_len;
	*pub_start_off = off;
	*pub_len = data_len;

	ret = 0;

out:
	return ret;
}

static int parse_pubkey_dsa(cert_parsing_ctx *ctx,
			    const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	const x509_u8 *buf = cert + off;
	x509_u32 pub_start_off = 0, pub_len = 0;
	int ret;

	if ((ctx == NULL) || (cert == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_pubkey_dsa_export_pub(buf, len, &pub_start_off, &pub_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->spki_alg_params.dsa.dsa_raw_pub_off = off + pub_start_off;
	ctx->spki_alg_params.dsa.dsa_raw_pub_len = pub_len;

out:
	return ret;
}

static int parse_x509_tbsCert_sig_AlgorithmIdentifier(cert_parsing_ctx *ctx,
						      const x509_u8 *cert, x509_u32 off, x509_u32 len,
						      const _sig_alg **alg,
						      x509_u32 *eaten)
{
	const _sig_alg *talg = NULL;
	const x509_u8 *buf = cert + off;
	x509_u32 saved_off = off;
	x509_u32 parsed = 0;
	x509_u32 hdr_len = 0;
	x509_u32 data_len = 0;
	x509_u32 param_len;
	x509_u32 oid_len = 0;
	int ret;

	if ((ctx == NULL) || (cert == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	parsed = hdr_len + data_len;

	buf += hdr_len;
	off += hdr_len;

	ret = parse_OID(buf, data_len, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	talg = find_sig_alg_by_oid(buf, oid_len);

	if (talg == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->tbs_sig_alg_oid_start = off;
	ctx->tbs_sig_alg_oid_len = oid_len;
	ctx->sig_alg = talg->sig_id;
	ctx->hash_alg = talg->hash_id;

	buf += oid_len;
	off += oid_len;
	param_len = data_len - oid_len;

	ret = talg->parse_algoid_sig_params(&ctx->sig_alg_params, &ctx->hash_alg, cert, off, param_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->tbs_sig_alg_oid_params_start = off;
	ctx->tbs_sig_alg_oid_params_len = param_len;

	*alg = talg;
	*eaten = parsed;
	ctx->tbs_sig_alg_start = saved_off;
	ctx->tbs_sig_alg_len = parsed;

	ret = 0;

out:
	return ret;
}

static int parse_x509_pubkey_AlgorithmIdentifier(cert_parsing_ctx *ctx,
						 const x509_u8 *cert, x509_u32 off, x509_u32 len,
						 const _pubkey_alg **alg,
						 x509_u32 *eaten)
{
	const _pubkey_alg *talg = NULL;
	const x509_u8 *buf = cert + off;
	x509_u32 saved_off = off;
	x509_u32 parsed = 0;
	x509_u32 hdr_len = 0;
	x509_u32 data_len = 0;
	x509_u32 param_len;
	x509_u32 oid_len = 0;
	int ret;

	if ((ctx == NULL) || (cert == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;

	ret = parse_OID(buf, data_len, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->spki_alg_oid_start = off;
	ctx->spki_alg_oid_len = oid_len;

	talg = find_pubkey_alg_by_oid(buf, oid_len);
	if (talg == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += oid_len;
	off += oid_len;
	param_len = data_len - oid_len;

	ret = talg->parse_algoid_pubkey_params(ctx, cert, off, param_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->spki_alg_oid_params_start = off;
	ctx->spki_alg_oid_params_len = param_len;

	parsed = hdr_len + data_len;

	*alg = talg;

	*eaten = parsed;

	ctx->spki_alg = talg->pubkey_id;
	ctx->spki_alg_oid_start = saved_off;
	ctx->spki_alg_oid_len = parsed;

	ret = 0;

out:
	return ret;
}

static int parse_x509_Validity(cert_parsing_ctx *ctx,
			       const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten)
{
	const x509_u8 *buf = cert + off;
	int ret;
	x509_u32 hdr_len = 0;
	x509_u32 remain = 0;
	x509_u32 data_len = 0;
	x509_u32 nb_len = 0, na_len = 0;
	x509_u16 na_year = 0, nb_year = 0;
	x509_u8 na_month = 0, na_day = 0, na_hour = 0, na_min = 0, na_sec = 0;
	x509_u8 nb_month = 0, nb_day = 0, nb_hour = 0, nb_min = 0, nb_sec = 0;
	x509_u8 t_type = 0;
	x509_u64 not_after, not_before;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	remain = data_len;

	ret = parse_Time(buf, remain, &t_type, &nb_len, &nb_year, &nb_month,
			 &nb_day, &nb_hour, &nb_min, &nb_sec);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = verify_correct_time_use(t_type, nb_year);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain -= nb_len;
	buf += nb_len;

	ret = parse_Time(buf, remain, &t_type, &na_len, &na_year, &na_month,
			 &na_day, &na_hour, &na_min, &na_sec);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = verify_correct_time_use(t_type, na_year);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain -= na_len;
	if (remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	not_after = time_components_to_comparable_u64(na_year, na_month, na_day,
						      na_hour, na_min, na_sec);

	not_before = time_components_to_comparable_u64(nb_year, nb_month, nb_day,
						       nb_hour, nb_min, nb_sec);

	if (not_before >= not_after) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->not_before = not_before;
	ctx->not_after = not_after;
	*eaten = hdr_len + data_len;

	ret = 0;

out:
	return ret;
}

static int parse_x509_subjectPublicKeyInfo(cert_parsing_ctx *ctx,
					   const x509_u8 *cert, x509_u32 off, x509_u32 len,
					   x509_u32 *eaten)
{
	x509_u32 hdr_len = 0, data_len = 0, parsed = 0, remain = 0;
	x509_u32 saved_off = off;
	const x509_u8 *buf = cert + off;
	const _pubkey_alg *alg = NULL;
	int ret;

	if ((ctx == NULL) || (cert == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	remain = data_len;
	off += hdr_len;

	ret = parse_x509_pubkey_AlgorithmIdentifier(ctx, cert, off, remain,
						    &alg, &parsed);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += parsed;
	remain -= parsed;
	off += parsed;

	if (!alg->parse_pubkey) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = alg->parse_pubkey(ctx, cert, off, remain);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->spki_pub_key_start = off;
	ctx->spki_pub_key_len = remain;
	ctx->spki_start = saved_off;
	ctx->spki_len = hdr_len + data_len;
	*eaten = hdr_len + data_len;

	ret = 0;

out:
	return ret;
}

#if 0

static int check_prefered_name_syntax(const x509_u8 *buf, x509_u32 len)
{

	ret = 0;

out:
	return ret;
}
#endif

static int parse_ext_AIA(cert_parsing_ctx ATTRIBUTE_UNUSED *ctx,
			 const x509_u8 *cert, x509_u32 off, x509_u32 len, int critical)
{
	int ret;

	if (ctx == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_AIA(cert, off, len, critical);
	if (ret) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

out:
	return ret;
}

static int parse_ext_AKI(cert_parsing_ctx *ctx,
			 const x509_u8 *cert, x509_u32 off, x509_u32 len, int critical)
{
	x509_u32 hdr_len = 0, data_len = 0;
	const x509_u8 *buf = cert + off;
	x509_u32 key_id_hdr_len = 0, key_id_data_len = 0, key_id_data_off = 0;
	x509_u32 gen_names_off = 0, gen_names_len = 0;
	x509_u32 cert_serial_off = 0, cert_serial_len = 0;
	x509_u32 remain;
	x509_u32 parsed = 0;
	int ret, has_keyIdentifier = 0, has_gen_names_and_serial = 0;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (critical) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;
	remain = data_len;

	if (len != (hdr_len + data_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, remain, CLASS_CONTEXT_SPECIFIC, 0,
			   &key_id_hdr_len, &key_id_data_len);
	if (!ret) {

		if (!key_id_data_len) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		key_id_data_off = off + key_id_hdr_len;
		buf += key_id_hdr_len + key_id_data_len;
		off += key_id_hdr_len + key_id_data_len;
		remain -= key_id_hdr_len + key_id_data_len;
		has_keyIdentifier = 1;
	}

	ret = parse_GeneralNames(buf, remain, CLASS_CONTEXT_SPECIFIC, 1,
				 &parsed);
	if (!ret) {
		gen_names_off = off;
		gen_names_len = parsed;

		buf += parsed;
		off += parsed;
		remain -= parsed;

		ret = parse_AKICertSerialNumber(cert, off, remain,
						CLASS_CONTEXT_SPECIFIC, 2,
						&cert_serial_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		has_gen_names_and_serial = 1;
		cert_serial_off = off;

		buf += cert_serial_len;
		off += cert_serial_len;
		remain -= cert_serial_len;
	}

	if (remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->aki_has_keyIdentifier = has_keyIdentifier;

	if (ctx->aki_has_keyIdentifier) {
		ctx->aki_keyIdentifier_start = key_id_data_off;

		ctx->aki_keyIdentifier_len = key_id_data_len;

	}
	ctx->aki_has_generalNames_and_serial = has_gen_names_and_serial;

	if (ctx->aki_has_generalNames_and_serial) {
		ctx->aki_generalNames_start = gen_names_off;

		ctx->aki_generalNames_len = gen_names_len;

		ctx->aki_serial_start = cert_serial_off + 2;  

		ctx->aki_serial_len = cert_serial_len - 2;

	}
	ctx->has_aki = 1;

	ret = 0;

out:
	return ret;
}

static int parse_ext_SKI(cert_parsing_ctx *ctx,
			 const x509_u8 *cert, x509_u32 off, x509_u32 len, int critical)
{
	x509_u32 key_id_hdr_len = 0, key_id_data_len = 0;
	const x509_u8 *buf = cert + off;
	x509_u32 remain;
	int ret;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = len;

#ifdef TEMPORARY_LAXIST_SKI_CRITICAL_FLAG_SET
	(void)critical;
#else
	if (critical) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
#endif

	ret = parse_id_len(buf, remain, CLASS_UNIVERSAL, ASN1_TYPE_OCTET_STRING,
			   &key_id_hdr_len, &key_id_data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len != (key_id_hdr_len + key_id_data_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (!key_id_data_len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain -= key_id_hdr_len + key_id_data_len;
	if (remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->has_ski = 1;
	ctx->ski_start = off + key_id_hdr_len;
	ctx->ski_len = key_id_data_len;
	ret = 0;

out:
	return ret;
}

#define KU_keyAgreement      0x0010
#define KU_keyCertSign       0x0020
#define KU_cRLSign           0x0040
#define KU_encipherOnly      0x0080
#define KU_decipherOnly      0x0100

static int parse_ext_keyUsage(cert_parsing_ctx *ctx,
			      const x509_u8 *cert, x509_u32 off, x509_u32 len,
			      int ATTRIBUTE_UNUSED critical)
{
	x509_u32 hdr_len = 0, data_len = 0;
	x509_u16 val = 0;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_BIT_STRING,
				   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len != (hdr_len + data_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	len -= hdr_len;

	ret = parse_nine_bit_named_bit_list(buf, data_len, &val);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if ((val & KU_decipherOnly) && !(val & KU_keyAgreement)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if ((val & KU_encipherOnly) && !(val & KU_keyAgreement)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->has_keyUsage = 1;
	ctx->keyCertSign_set = !!(val & KU_keyCertSign);
	ctx->cRLSign_set = !!(val & KU_cRLSign);

	ret = 0;

out:
	return ret;
}

static int parse_CPSuri(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten)
{
	int ret;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_ia5_string(buf, len, 1, 65534);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = len;

	ret = 0;

out:
	return ret;
}

static int parse_NoticeReference(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 remain, parsed = 0, saved_len = 0, hdr_len = 0, data_len = 0;
	int ret;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = len;

	ret = parse_id_len(buf, remain, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	saved_len = hdr_len + data_len;
	remain = data_len;
	buf += hdr_len;

	ret = parse_DisplayText(buf, remain, &parsed);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain -= parsed;
	buf += parsed;

	ret = parse_id_len(buf, remain, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain -= hdr_len;
	buf += hdr_len;

	if (remain != data_len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	while (remain) {

		ret = parse_integer(buf, remain,
				    CLASS_UNIVERSAL, ASN1_TYPE_INTEGER,
				    &hdr_len, &data_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		parsed = hdr_len + data_len;
		remain -= parsed;
		buf += parsed;
	}

	*eaten = saved_len;

	ret = 0;

out:
	return ret;
}

static int parse_UserNotice(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 hdr_len = 0, data_len = 0, remain = 0, parsed = 0;
	int ret;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = len;

	ret = parse_id_len(buf, remain, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain -= hdr_len;
	buf += hdr_len;

	if (!data_len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_NoticeReference(buf, remain, &parsed);
	if (!ret) {
		remain -= parsed;
		buf += parsed;
	}

	ret = parse_DisplayText(buf, remain, &parsed);
	if (!ret) {
		remain -= parsed;
		buf += parsed;
	}

	if (remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = hdr_len + data_len;

	ret = 0;

out:
	return ret;
}

static int parse_policyQualifierInfo(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 hdr_len = 0, data_len = 0, oid_len = 0, remain = 0;
	x509_u8 id_qt_cps_oid[] = { 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05,
			       0x07, 0x02, 0x01 };
	x509_u8 id_qt_unotice_oid[] = { 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05,
				   0x07, 0x02, 0x02 };
	int ret;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = data_len;
	buf += hdr_len;

	ret = parse_OID(buf, remain, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if ((oid_len == sizeof(id_qt_cps_oid)) &&
	    !bufs_differ(buf, id_qt_cps_oid, oid_len)) { 
		x509_u32 cpsuri_len = 0;

		buf += oid_len;
		remain -= oid_len;

		ret = parse_CPSuri(buf, remain, &cpsuri_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		remain -= cpsuri_len;
		buf += cpsuri_len;

	} else if ((oid_len == sizeof(id_qt_unotice_oid)) &&
	    !bufs_differ(buf, id_qt_unotice_oid, oid_len)) { 
		x509_u32 cpsunotice_len = 0;

		buf += oid_len;
		remain -= oid_len;

		ret = parse_UserNotice(buf, remain, &cpsunotice_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		remain -= cpsunotice_len;
		buf += cpsunotice_len;

	} else {                                        
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = hdr_len + data_len;

	ret = 0;

out:
	return ret;
}

static int parse_PolicyInformation(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 hdr_len = 0, data_len = 0, oid_len = 0, saved_pi_len, remain;
	int ret;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	saved_pi_len = hdr_len + data_len;

	remain = data_len;
	buf += hdr_len;

	ret = parse_OID(buf, remain, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain -= oid_len;
	buf += oid_len;

	if (remain) {

		ret = parse_id_len(buf, remain,
				   CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
				   &hdr_len, &data_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		remain -= hdr_len;
		buf += hdr_len;

		if (remain != data_len) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		while (remain) {
			x509_u32 pqi_len = 0;

			ret = parse_policyQualifierInfo(buf, remain, &pqi_len);
			if (ret) {
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
			}

			remain -= pqi_len;
			buf += pqi_len;
		}
	}

	*eaten = saved_pi_len;

	ret = 0;

out:
	return ret;
}

static int parse_ext_certPolicies(cert_parsing_ctx ATTRIBUTE_UNUSED *ctx,
				  const x509_u8 *cert, x509_u32 off, x509_u32 len,
				  int ATTRIBUTE_UNUSED critical)
{
	x509_u32 remain = 0, data_len = 0, hdr_len = 0, eaten = 0;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;
	remain = data_len;

	if (len != (hdr_len + data_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	while (remain) {
		ret = parse_PolicyInformation(buf, remain, &eaten);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		remain -= eaten;
		off += eaten;
		buf += eaten;
	}

	ret = 0;

out:
	return ret;
}

static int parse_ext_policyMapping(cert_parsing_ctx ATTRIBUTE_UNUSED *ctx,
				   const x509_u8 *cert, x509_u32 off, x509_u32 len,
				   int critical)
{
	x509_u32 remain = 0, data_len = 0, hdr_len = 0, eaten = 0;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (!critical) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;
	remain = data_len;

	if (len != (hdr_len + data_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	while (remain) {
		ret = parse_id_len(buf, remain,
				   CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
				   &hdr_len, &data_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += hdr_len;
		off += hdr_len;
		remain -= hdr_len;

		ret = parse_OID(buf, data_len, &eaten);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += eaten;
		off += eaten;
		remain -= eaten;
		data_len -= eaten;

		ret = parse_OID(buf, data_len, &eaten);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		data_len -= eaten;
		if (data_len) {

			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += eaten;
		off += eaten;
		remain -=  eaten;
	}

	ret = 0;

out:
	return ret;
}

static int parse_ext_SAN(cert_parsing_ctx *ctx,
			 const x509_u8 *cert, x509_u32 off, x509_u32 len,
			 int critical)
{
	x509_u32 data_len = 0, hdr_len = 0, remain = 0, eaten = 0;
	const x509_u8 *buf = cert + off;
	int ret, empty_gen_name;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (!data_len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;
	remain = data_len;

	if (len != (hdr_len + data_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	while (remain) {
		empty_gen_name = 0;
		ret = parse_GeneralName(buf, remain, &eaten, &empty_gen_name);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		if (empty_gen_name) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		if (buf[0] == NAME_TYPE_iPAddress) {
			switch (eaten) {
			case 6: 
				break;
			case 18: 
				break;
			default: 
				ret = -X509_FILE_LINE_NUM_ERR;
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
				break;
			}
		}

		remain -= eaten;
		buf += eaten;
	}

	ctx->has_san = 1;
	ctx->san_critical = critical;

	ret = 0;

out:
	return ret;
}

static int parse_ext_IAN(cert_parsing_ctx ATTRIBUTE_UNUSED *ctx,
			 const x509_u8 *cert, x509_u32 off, x509_u32 len,
			 int ATTRIBUTE_UNUSED critical)
{
	x509_u32 data_len = 0, hdr_len = 0, remain = 0, eaten = 0;
	const x509_u8 *buf = cert + off;
	int ret, unused = 0;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (!data_len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;
	remain = data_len;

	if (len != (hdr_len + data_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	while (remain) {
		ret = parse_GeneralName(buf, remain, &eaten, &unused);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		remain -= eaten;
		off += eaten;
		buf += eaten;
	}

	ret = 0;

out:
	return ret;
}

static int parse_ext_subjectDirAttr(cert_parsing_ctx ATTRIBUTE_UNUSED *ctx,
				    const x509_u8 *cert, x509_u32 off, x509_u32 len,
				    int critical)
{
	x509_u32 hdr_len = 0, data_len = 0, oid_len = 0, remain = 0;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (critical) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;
	remain = data_len;

	if (len != (hdr_len + data_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	while (remain) {

		ret = parse_id_len(buf, remain,
				   CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
				   &hdr_len, &data_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += hdr_len;
		off += hdr_len;
		remain -= hdr_len;

		ret = parse_OID(buf, data_len, &oid_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		remain -= data_len;
		off += data_len;
		buf += data_len;
	}

	ret = 0;

out:
	return ret;
}

static int parse_ext_basicConstraints(cert_parsing_ctx *ctx,
				      const x509_u8 *cert, x509_u32 off, x509_u32 len,
				      int critical)
{
	x509_u32 hdr_len = 0, data_len = 0;
	const x509_u8 ca_true_wo_plc[] = { 0x01, 0x01, 0xff };
	const x509_u8 ca_true_w_plc[] = { 0x01, 0x01, 0xff, 0x02, 0x01 };
	const x509_u8 *buf = cert + off;
	int ret;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->bc_critical = critical;

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;

	if (len != (hdr_len + data_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	switch (data_len) {
	case 0: 
		ret = 0;
		break;
	case 3: 

		ret = bufs_differ(buf, ca_true_wo_plc, 3);
		if (!ret) {
			ctx->ca_true = 1;
			break;
		}

#ifdef TEMPORARY_LAXIST_CA_BASIC_CONSTRAINTS_BOOLEAN_EXPLICIT_FALSE
		{
			const x509_u8 ca_false_explicit_wo_plc[] = { 0x01, 0x01, 0x00 };

			ret = bufs_differ(buf, ca_false_explicit_wo_plc, 3);
			if (!ret) {
				break;
			}
		}
#endif

		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
		break;
	case 6: 
		ret = bufs_differ(buf, ca_true_w_plc, 5);
		if (ret) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		if (buf[5] & 0x80) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
		ctx->ca_true = 1;
		ctx->pathLenConstraint_set = 1;
		break;
	default: 
		ret = -X509_FILE_LINE_NUM_ERR;
		break;
	}

out:
	return ret;
}

static int parse_GeneralSubtrees(const x509_u8 *buf, x509_u32 len)
{
	x509_u32 hdr_len = 0, remain = 0, grabbed = 0, data_len = 0;
	int ret, unused = 0;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	remain = data_len;

	ret = parse_GeneralName(buf, remain, &grabbed, &unused);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += grabbed;
	remain -= grabbed;

	ret = parse_id_len(buf, remain, CLASS_CONTEXT_SPECIFIC, 0,
			   &hdr_len, &data_len);
	if (!ret) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, remain, CLASS_CONTEXT_SPECIFIC, 1,
			   &hdr_len, &data_len);
	if (!ret) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

static int parse_ext_nameConstraints(cert_parsing_ctx *ctx,
				     const x509_u8 *cert, x509_u32 off, x509_u32 len, int critical)
{
	x509_u32 remain = 0, hdr_len = 0, data_len = 0;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (!critical) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &remain);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;

	if (!remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, remain, CLASS_CONTEXT_SPECIFIC, 0,
			   &hdr_len, &data_len);
	if (!ret) {
		buf += hdr_len;
		off += hdr_len;
		remain -= hdr_len;

		ret = parse_GeneralSubtrees(buf, data_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += data_len;
		off += data_len;
		remain -= data_len;
	}

	ret = parse_id_len(buf, remain, CLASS_CONTEXT_SPECIFIC, 1,
			   &hdr_len, &data_len);
	if (!ret) {
		buf += hdr_len;
		off += hdr_len;
		remain -= hdr_len;

		ret = parse_GeneralSubtrees(buf, data_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += data_len;
		off += data_len;
		remain -= data_len;
	}

	if (remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->has_name_constraints = 1;

	ret = 0;

out:
	return ret;
}

static int parse_ext_policyConstraints(cert_parsing_ctx ATTRIBUTE_UNUSED *ctx,
				       const x509_u8 *cert, x509_u32 off, x509_u32 len,
				       int critical)
{
	x509_u32 data_len = 0, hdr_len = 0, remain = 0, parsed = 0;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (!critical) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;
	remain = data_len;

	ret = parse_integer(buf, remain, CLASS_CONTEXT_SPECIFIC, 0,
			    &hdr_len, &data_len);
	if (!ret) {

		parsed = hdr_len + data_len;
		if (parsed != 3) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += parsed;
		off += parsed;
		remain -= parsed;
	}

	ret = parse_integer(buf, remain, CLASS_CONTEXT_SPECIFIC, 1,
			    &hdr_len, &data_len);
	if (!ret) {

		parsed = hdr_len + data_len;
		if (parsed != 3) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += parsed;
		off += parsed;
		remain -= parsed;
	}

	if (remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
       return ret;
}

static const x509_u8 _id_kp_anyEKU[] =       { 0x06, 0x08, 0x2b, 0x06,
					  0x01, 0x05, 0x05, 0x07,
					  0x03, 0x00 };
static const x509_u8 _id_kp_serverAuth[] =   { 0x06, 0x08, 0x2b, 0x06,
					  0x01, 0x05, 0x05, 0x07,
					  0x03, 0x01 };
static const x509_u8 _id_kp_clientAuth[] =   { 0x06, 0x08, 0x2b, 0x06,
					  0x01, 0x05, 0x05, 0x07,
					  0x03, 0x02 };
static const x509_u8 _id_kp_codeSigning[] =  { 0x06, 0x08, 0x2b, 0x06,
					  0x01, 0x05, 0x05, 0x07,
					  0x03, 0x03 };
static const x509_u8 _id_kp_emailProt[] =    { 0x06, 0x08, 0x2b, 0x06,
					  0x01, 0x05, 0x05, 0x07,
					  0x03, 0x04 };
static const x509_u8 _id_kp_timeStamping[] = { 0x06, 0x08, 0x2b, 0x06,
					  0x01, 0x05, 0x05, 0x07,
					  0x03, 0x08 };
static const x509_u8 _id_kp_OCSPSigning[] =  { 0x06, 0x08, 0x2b, 0x06,
					  0x01, 0x05, 0x05, 0x07,
					  0x03, 0x09 };
static const x509_u8 _id_kp_ns_SGC[] = {  0x06, 0x09, 0x60, 0x86, 0x48,
				     0x01, 0x86, 0xF8, 0x42, 0x04,
				     0x01  };
static const x509_u8 _id_kp_ms_SGC[] = {  0x06, 0x0A, 0x2B, 0x06, 0x01,
				     0x04, 0x01, 0x82, 0x37, 0x0A,
				     0x03, 0x03,   };

typedef struct {
	const x509_u8 *oid;
	x509_u8 oid_len;
} _kp_oid;

static const _kp_oid known_kp_oids[] = {
	{ .oid = _id_kp_anyEKU,
	  .oid_len = sizeof(_id_kp_anyEKU),
	},
	{ .oid = _id_kp_serverAuth,
	  .oid_len = sizeof(_id_kp_serverAuth),
	},
	{ .oid = _id_kp_clientAuth,
	  .oid_len = sizeof(_id_kp_clientAuth),
	},
	{ .oid = _id_kp_codeSigning,
	  .oid_len = sizeof(_id_kp_codeSigning),
	},
	{ .oid = _id_kp_emailProt,
	  .oid_len = sizeof(_id_kp_emailProt),
	},
	{ .oid = _id_kp_timeStamping,
	  .oid_len = sizeof(_id_kp_timeStamping),
	},
	{ .oid = _id_kp_OCSPSigning,
	  .oid_len = sizeof(_id_kp_OCSPSigning),
	},
	{ .oid = _id_kp_ns_SGC,
	  .oid_len = sizeof(_id_kp_ns_SGC),
	},
	{ .oid = _id_kp_ms_SGC,
	  .oid_len = sizeof(_id_kp_ms_SGC),
	},
};

const x509_u16 num_known_kp_oids = (sizeof(known_kp_oids) / sizeof(known_kp_oids[0]));

static const _kp_oid * find_kp_by_oid(const x509_u8 *buf, x509_u32 len)
{
	const _kp_oid *found = NULL;
	const _kp_oid *cur = NULL;
	x509_u8 k;

	if ((buf == NULL) || (len == 0)) {
		goto out;
	}

	for (k = 0; k < num_known_kp_oids; k++) {
		int ret;

		cur = &known_kp_oids[k];

		if (cur->oid_len != len) {
			continue;
		}

		ret = !bufs_differ(cur->oid, buf, cur->oid_len);
		if (ret) {
			found = cur;
			break;
		}
	}

out:
	return found;
}

static int parse_ext_EKU(cert_parsing_ctx ATTRIBUTE_UNUSED *ctx,
			 const x509_u8 *cert, x509_u32 off, x509_u32 len,
			 int critical)
{
	x509_u32 remain = 0, data_len = 0, hdr_len = 0, oid_len = 0;
	const x509_u8 *buf = cert + off;
	const _kp_oid *kp = NULL;
	int ret;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret || (data_len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;
	remain = data_len;

	if (len != (hdr_len + data_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	while (remain) {
		ret = parse_OID(buf, remain, &oid_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		kp = find_kp_by_oid(buf, oid_len);
		if (kp == NULL) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		if ((kp->oid == _id_kp_anyEKU) && critical) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += oid_len;
		off += oid_len;
		remain -= oid_len;
	}

	ctx->has_eku = 1;

	ret = 0;

out:
	return ret;
}

static int parse_ext_CRLDP(cert_parsing_ctx *ctx,
			   const x509_u8 *cert, x509_u32 off, x509_u32 len,
			   int ATTRIBUTE_UNUSED critical)
{
	x509_u32 hdr_len = 0, data_len = 0, remain;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len,
			   CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	remain = data_len;

	if (len != (hdr_len + data_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->has_crldp = 1;
	ctx->one_crldp_has_all_reasons = 0;

	while (remain) {
		int crldp_has_all_reasons = 0;
		x509_u32 eaten = 0;

		ret = parse_DistributionPoint(buf, remain,
					      &crldp_has_all_reasons, &eaten);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		if (crldp_has_all_reasons) {
			ctx->one_crldp_has_all_reasons = 1;
		}

		remain -= eaten;
		buf += eaten;
	}

	ret = 0;

out:
	return ret;
}

#define MAX_INHIBITANYPOLICY 64
static int parse_ext_inhibitAnyPolicy(cert_parsing_ctx ATTRIBUTE_UNUSED *ctx,
				      const x509_u8 *cert, x509_u32 off, x509_u32 len,
				      int critical)
{
	const x509_u8 *buf = cert + off;
	x509_u32 eaten = 0, hdr_len = 0, data_len = 0;
	int ret;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (!critical) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_integer(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_INTEGER,
			    &hdr_len, &data_len);
	if (ret) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	eaten = hdr_len + data_len;

	if (eaten != 3) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if ((buf[2] & 0x80) || (buf[2] > MAX_INHIBITANYPOLICY)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (eaten != len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

static const x509_u8 _ext_oid_AIA[] =               { 0x06, 0x08, 0x2b, 0x06, 0x01,
						 0x05, 0x05, 0x07, 0x01, 0x01 };
static const x509_u8 _ext_oid_subjectDirAttr[] =    { 0x06, 0x03, 0x55, 0x1d, 0x09 };
static const x509_u8 _ext_oid_SKI[] =               { 0x06, 0x03, 0x55, 0x1d, 0x0e };
static const x509_u8 _ext_oid_keyUsage[] =          { 0x06, 0x03, 0x55, 0x1d, 0x0f };
static const x509_u8 _ext_oid_SAN[] =               { 0x06, 0x03, 0x55, 0x1d, 0x11 };
static const x509_u8 _ext_oid_IAN[] =               { 0x06, 0x03, 0x55, 0x1d, 0x12 };
static const x509_u8 _ext_oid_basicConstraints[] =  { 0x06, 0x03, 0x55, 0x1d, 0x13 };
static const x509_u8 _ext_oid_nameConstraints[] =   { 0x06, 0x03, 0x55, 0x1d, 0x1e };
static const x509_u8 _ext_oid_CRLDP[] =             { 0x06, 0x03, 0x55, 0x1d, 0x1f };
static const x509_u8 _ext_oid_certPolicies[] =      { 0x06, 0x03, 0x55, 0x1d, 0x20 };
static const x509_u8 _ext_oid_policyMapping[] =     { 0x06, 0x03, 0x55, 0x1d, 0x21 };
static const x509_u8 _ext_oid_AKI[] =               { 0x06, 0x03, 0x55, 0x1d, 0x23 };
static const x509_u8 _ext_oid_policyConstraints[] = { 0x06, 0x03, 0x55, 0x1d, 0x24 };
static const x509_u8 _ext_oid_EKU[] =               { 0x06, 0x03, 0x55, 0x1d, 0x25 };
static const x509_u8 _ext_oid_FreshestCRL[] =       { 0x06, 0x03, 0x55, 0x1d, 0x2e };
static const x509_u8 _ext_oid_inhibitAnyPolicy[] =  { 0x06, 0x03, 0x55, 0x1d, 0x36 };

static int parse_ext_bad_oid(cert_parsing_ctx ATTRIBUTE_UNUSED *ctx,
			     const x509_u8 *cert, x509_u32 ATTRIBUTE_UNUSED off, x509_u32 len,
			     int ATTRIBUTE_UNUSED critical)
{
	int ret;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

#ifdef TEMPORARY_LAXIST_HANDLE_COMMON_UNSUPPORTED_EXT_OIDS

static const x509_u8 _ext_oid_bad_ct1[] = {
	0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07,
	0x01, 0x01
};
static const x509_u8 _ext_oid_bad_ct_poison[] = {
	0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0xd6,
	0x79, 0x02, 0x04, 0x03
};
static const x509_u8 _ext_oid_bad_ct_enabled[] = {
	0x06, 0x0a, 0x2b, 0x06,	 0x01, 0x04, 0x01, 0xd6,
	0x79, 0x02, 0x04, 0x02
};
static const x509_u8 _ext_oid_bad_ns_cert_type[] = {
	0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x86, 0xf8,
	0x42, 0x01, 0x01
};
static const x509_u8 _ext_oid_bad_szOID_ENROLL[] = {
	0x06, 0x09, 0x2b, 0x06,  0x01, 0x04, 0x01, 0x82,
	0x37, 0x14, 0x02
};
static const x509_u8 _ext_oid_bad_smime_cap[] = {
	0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
	0x01, 0x09, 0x0f
};
static const x509_u8 _ext_oid_bad_ns_comment[] = {
	0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x86, 0xf8,
	0x42, 0x01, 0x0d
};
static const x509_u8 _ext_oid_bad_deprecated_AKI[] = {
	0x06, 0x03, 0x55, 0x1d, 0x01
};
static const x509_u8 _ext_oid_bad_szOID_CERT_TEMPLATE[] = {
	0x06, 0x09, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82,
	0x37, 0x15, 0x07
};
static const x509_u8 _ext_oid_bad_pkixFixes[] = {
	0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x97,
	0x55, 0x03, 0x01, 0x05
};
static const x509_u8 _ext_oid_bad_ns_ca_policy_url[] = {
	0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x86, 0xf8,
	0x42, 0x01, 0x08
};
static const x509_u8 _ext_oid_bad_szOID_CERTSRV_CA_VERS[] = {
	0x06, 0x09, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82,
	0x37, 0x15, 0x01
};
static const x509_u8 _ext_oid_bad_szOID_APP_CERT_POL[] = {
	0x06, 0x09, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82,
	0x37, 0x15, 0x0a
};
static const x509_u8 _ext_oid_bad_priv_key_usage_period[] = {
	0x06, 0x03, 0x55, 0x1d, 0x10
};
static const x509_u8 _ext_oid_bad_subject_signing_tool[] = {
	0x06, 0x05, 0x2a, 0x85,	0x03, 0x64, 0x6f
};
static const x509_u8 _ext_oid_bad_issuer_signing_tool[] = {
	0x06, 0x05, 0x2a, 0x85,	0x03, 0x64, 0x70
};
static const x509_u8 _ext_oid_bad_szOID_CERTSRV_PREVIOUS_CERT_HASH[] = {
	0x06, 0x09, 0x2b, 0x06,	0x01, 0x04, 0x01,
	0x82, 0x37, 0x15, 0x02
};
#endif

typedef struct {
	const x509_u8 *oid;
	x509_u32 oid_len;
	int (*parse_ext_params)(cert_parsing_ctx *ctx,
				const x509_u8 *cert, x509_u32 off, x509_u32 len, int critical);
} _ext_oid;

static const _ext_oid generic_unsupported_ext_oid = {
	.oid = NULL,
	.oid_len = 0,
	.parse_ext_params = parse_ext_bad_oid
};

static const _ext_oid known_ext_oids[] = {
	{ .oid = _ext_oid_AIA,
	  .oid_len = sizeof(_ext_oid_AIA),
	  .parse_ext_params = parse_ext_AIA,
	},
	{ .oid = _ext_oid_AKI,
	  .oid_len = sizeof(_ext_oid_AKI),
	  .parse_ext_params = parse_ext_AKI,
	},
	{ .oid = _ext_oid_SKI,
	  .oid_len = sizeof(_ext_oid_SKI),
	  .parse_ext_params = parse_ext_SKI,
	},
	{ .oid = _ext_oid_keyUsage,
	  .oid_len = sizeof(_ext_oid_keyUsage),
	  .parse_ext_params = parse_ext_keyUsage,
	},
	{ .oid = _ext_oid_certPolicies,
	  .oid_len = sizeof(_ext_oid_certPolicies),
	  .parse_ext_params = parse_ext_certPolicies,
	},
	{ .oid = _ext_oid_policyMapping,
	  .oid_len = sizeof(_ext_oid_policyMapping),
	  .parse_ext_params = parse_ext_policyMapping,
	},
	{ .oid = _ext_oid_SAN,
	  .oid_len = sizeof(_ext_oid_SAN),
	  .parse_ext_params = parse_ext_SAN,
	},
	{ .oid = _ext_oid_IAN,
	  .oid_len = sizeof(_ext_oid_IAN),
	  .parse_ext_params = parse_ext_IAN,
	},
	{ .oid = _ext_oid_subjectDirAttr,
	  .oid_len = sizeof(_ext_oid_subjectDirAttr),
	  .parse_ext_params = parse_ext_subjectDirAttr,
	},
	{ .oid = _ext_oid_basicConstraints,
	  .oid_len = sizeof(_ext_oid_basicConstraints),
	  .parse_ext_params = parse_ext_basicConstraints,
	},
	{ .oid = _ext_oid_nameConstraints,
	  .oid_len = sizeof(_ext_oid_nameConstraints),
	  .parse_ext_params = parse_ext_nameConstraints,
	},
	{ .oid = _ext_oid_policyConstraints,
	  .oid_len = sizeof(_ext_oid_policyConstraints),
	  .parse_ext_params = parse_ext_policyConstraints,
	},
	{ .oid = _ext_oid_EKU,
	  .oid_len = sizeof(_ext_oid_EKU),
	  .parse_ext_params = parse_ext_EKU,
	},
	{ .oid = _ext_oid_CRLDP,
	  .oid_len = sizeof(_ext_oid_CRLDP),
	  .parse_ext_params = parse_ext_CRLDP,
	},
	{ .oid = _ext_oid_inhibitAnyPolicy,
	  .oid_len = sizeof(_ext_oid_inhibitAnyPolicy),
	  .parse_ext_params = parse_ext_inhibitAnyPolicy,
	},
	{ .oid = _ext_oid_FreshestCRL,
	  .oid_len = sizeof(_ext_oid_FreshestCRL),
	  .parse_ext_params = parse_ext_CRLDP,
	},
#ifdef TEMPORARY_LAXIST_HANDLE_COMMON_UNSUPPORTED_EXT_OIDS
	{ .oid = _ext_oid_bad_ct1,
	  .oid_len = sizeof(_ext_oid_bad_ct1),
	  .parse_ext_params = parse_ext_bad_oid,
	},
	{ .oid = _ext_oid_bad_ct_poison,
	  .oid_len = sizeof(_ext_oid_bad_ct_poison),
	  .parse_ext_params = parse_ext_bad_oid,
	},
	{ .oid = _ext_oid_bad_ct_enabled,
	  .oid_len = sizeof(_ext_oid_bad_ct_enabled),
	  .parse_ext_params = parse_ext_bad_oid,
	},
	{ .oid = _ext_oid_bad_ns_cert_type,
	  .oid_len = sizeof(_ext_oid_bad_ns_cert_type),
	  .parse_ext_params = parse_ext_bad_oid,
	},
	{ .oid = _ext_oid_bad_szOID_ENROLL,
	  .oid_len = sizeof(_ext_oid_bad_szOID_ENROLL),
	  .parse_ext_params = parse_ext_bad_oid,
	},
	{ .oid = _ext_oid_bad_smime_cap,
	  .oid_len = sizeof(_ext_oid_bad_smime_cap),
	  .parse_ext_params = parse_ext_bad_oid,
	},
	{ .oid = _ext_oid_bad_ns_comment,
	  .oid_len = sizeof(_ext_oid_bad_ns_comment),
	  .parse_ext_params = parse_ext_bad_oid,
	},
	{ .oid = _ext_oid_bad_deprecated_AKI,
	  .oid_len = sizeof(_ext_oid_bad_deprecated_AKI),
	  .parse_ext_params = parse_ext_bad_oid,
	},
	{ .oid = _ext_oid_bad_szOID_CERT_TEMPLATE,
	  .oid_len = sizeof(_ext_oid_bad_szOID_CERT_TEMPLATE),
	  .parse_ext_params = parse_ext_bad_oid,
	},
	{ .oid = _ext_oid_bad_pkixFixes,
	  .oid_len = sizeof(_ext_oid_bad_pkixFixes),
	  .parse_ext_params = parse_ext_bad_oid,
	},
	{ .oid = _ext_oid_bad_ns_ca_policy_url,
	  .oid_len = sizeof(_ext_oid_bad_ns_ca_policy_url),
	  .parse_ext_params = parse_ext_bad_oid,
	},
	{ .oid = _ext_oid_bad_szOID_CERTSRV_CA_VERS,
	  .oid_len = sizeof(_ext_oid_bad_szOID_CERTSRV_CA_VERS),
	  .parse_ext_params = parse_ext_bad_oid,
	},
	{ .oid = _ext_oid_bad_szOID_APP_CERT_POL,
	  .oid_len = sizeof(_ext_oid_bad_szOID_APP_CERT_POL),
	  .parse_ext_params = parse_ext_bad_oid,
	},
	{ .oid = _ext_oid_bad_priv_key_usage_period,
	  .oid_len = sizeof(_ext_oid_bad_priv_key_usage_period),
	  .parse_ext_params = parse_ext_bad_oid,
	},
	{ .oid = _ext_oid_bad_subject_signing_tool,
	  .oid_len = sizeof(_ext_oid_bad_subject_signing_tool),
	  .parse_ext_params = parse_ext_bad_oid,
	},
	{ .oid = _ext_oid_bad_issuer_signing_tool,
	  .oid_len = sizeof(_ext_oid_bad_issuer_signing_tool),
	  .parse_ext_params = parse_ext_bad_oid,
	},
	{ .oid = _ext_oid_bad_szOID_CERTSRV_PREVIOUS_CERT_HASH,
	  .oid_len = sizeof(_ext_oid_bad_szOID_CERTSRV_PREVIOUS_CERT_HASH),
	  .parse_ext_params = parse_ext_bad_oid,
	},
#endif
};

#define NUM_KNOWN_EXT_OIDS (sizeof(known_ext_oids) / sizeof(known_ext_oids[0]))

#define MAX_EXT_NUM_PER_CERT NUM_KNOWN_EXT_OIDS

static _ext_oid const * find_ext_by_oid(const x509_u8 *buf, x509_u32 len)
{
	const _ext_oid *found = NULL;
	const _ext_oid *cur = NULL;
	x509_u16 k;

	if ((buf == NULL) || (len == 0)) {
		goto out;
	}

	for (k = 0; k < NUM_KNOWN_EXT_OIDS; k++) {
		int ret;

		cur = &known_ext_oids[k];

		if (cur->oid_len != len) {
			continue;
		}

		ret = !bufs_differ(cur->oid, buf, cur->oid_len);
		if (ret) {
			found = cur;
			break;
		}
	}

out:
	return found;
}

static int check_record_ext_unknown(const _ext_oid *ext,
				    const _ext_oid **parsed_oid_list)
{
	x509_u16 pos = 0;
	int ret;

	while (pos < MAX_EXT_NUM_PER_CERT) {

		if (parsed_oid_list[pos] == NULL) {
			parsed_oid_list[pos] = ext;
			break;
		}

		if (ext == parsed_oid_list[pos]) {
			ret = -X509_FILE_LINE_NUM_ERR;
			goto out;
		}

		pos += 1;
	}

	if (pos >= MAX_EXT_NUM_PER_CERT) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

static int parse_x509_cert_Extension(cert_parsing_ctx *ctx,
				     const x509_u8 *cert, x509_u32 off, x509_u32 len,
				     const _ext_oid **parsed_oid_list,
				     x509_u32 *eaten)
{
	x509_u32 data_len = 0, hdr_len = 0, remain = 0;
	x509_u32 ext_hdr_len = 0, ext_data_len = 0, oid_len = 0;
	x509_u32 saved_ext_len = 0, parsed = 0;
	const x509_u8 *buf = cert + off;
	const _ext_oid *ext = NULL;
	int critical = 0;
	int ret;

	if ((cert == NULL) || (len == 0) || (ctx == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = len;

	ret = parse_id_len(buf, remain,
			   CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &ext_hdr_len, &ext_data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += ext_hdr_len;
	off += ext_hdr_len;
	remain -= ext_hdr_len;
	saved_ext_len = ext_hdr_len + ext_data_len;

	ret = parse_OID(buf, ext_data_len, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ext = find_ext_by_oid(buf, oid_len);
	if (ext == NULL) {
#ifndef TEMPORARY_LAXIST_HANDLE_ALL_REMAINING_EXT_OIDS
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
#else
		ext = &generic_unsupported_ext_oid;
#endif
	}

	if (ext != &generic_unsupported_ext_oid) {

		ret = check_record_ext_unknown(ext, parsed_oid_list);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
	}

	buf += oid_len;
	off += oid_len;
	ext_data_len -= oid_len;

	ret = parse_boolean(buf, ext_data_len, &parsed);
	if (ret) {

		if (ext_data_len && (buf[0] == ASN1_TYPE_BOOLEAN)) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
	} else {

		if (parsed != 3) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

#ifndef TEMPORARY_LAXIST_EXTENSION_CRITICAL_FLAG_BOOLEAN_EXPLICIT_FALSE
		if (buf[2] == 0x00) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
#endif

		critical = 1;

		buf += parsed;
		off += parsed;
		ext_data_len -= parsed;
	}

	ret = parse_id_len(buf, ext_data_len,
			   CLASS_UNIVERSAL, ASN1_TYPE_OCTET_STRING,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;
	ext_data_len -= hdr_len;

	if (data_len != ext_data_len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = ext->parse_ext_params(ctx, cert, off, ext_data_len, critical);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = saved_ext_len;
	ret = 0;

out:
	return ret;
}

static int parse_x509_cert_Extensions(cert_parsing_ctx *ctx,
				      const x509_u8 *cert, x509_u32 off, x509_u32 len,
				      x509_u32 *eaten)
{
	x509_u32 data_len = 0, hdr_len = 0, remain = 0;
	const x509_u8 *buf = cert + off;
	x509_u32 saved_len = 0;
	const _ext_oid *parsed_oid_list[MAX_EXT_NUM_PER_CERT];
	int ret;
	x509_u16 i;

	if ((cert == NULL) || (len == 0) || (ctx == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_explicit_id_len(buf, len, 3,
				    CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
				    &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = data_len;
	buf += hdr_len;
	off += hdr_len;

	saved_len = hdr_len + data_len;

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	for (i = 0; i < MAX_EXT_NUM_PER_CERT; i++) {
		parsed_oid_list[i] = NULL;
	}

	while (remain) {
		x509_u32 ext_len = 0;

		ret = parse_x509_cert_Extension(ctx, cert, off, remain,
						parsed_oid_list, &ext_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		remain -= ext_len;
		buf += ext_len;
		off += ext_len;
	}

	if (ctx->empty_subject) {
		if (!ctx->has_san) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
		if (!ctx->san_critical) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
	}

	*eaten = saved_len;

	ret = 0;

out:
	return ret;
}

static int parse_x509_tbsCertificate(cert_parsing_ctx *ctx,
				     const x509_u8 *cert, x509_u32 off, x509_u32 len,
				     const _sig_alg **sig_alg, x509_u32 *eaten)
{
	x509_u32 tbs_data_len = 0;
	x509_u32 tbs_hdr_len = 0;
	x509_u32 tbs_cert_len = 0;
	x509_u32 remain = 0;
	x509_u32 parsed = 0;
	x509_u32 cur_off = off;
	const x509_u8 *buf = cert + cur_off;
	const x509_u8 *subject_ptr, *issuer_ptr;
	x509_u32 subject_len, issuer_len;
	const _sig_alg *alg = NULL;
	int ret, empty_issuer = 1;

	if ((ctx == NULL) || (cert == NULL) || (len == 0) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &tbs_hdr_len, &tbs_data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	tbs_cert_len = tbs_hdr_len + tbs_data_len;
	buf += tbs_hdr_len;
	cur_off += tbs_hdr_len;
	remain = tbs_data_len;

	ret = parse_x509_cert_Version(cert, cur_off, remain, &ctx->version, &parsed);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += parsed;
	cur_off += parsed;
	remain -= parsed;

	ret = parse_CertSerialNumber(ctx, cert, cur_off, remain,
				     CLASS_UNIVERSAL, ASN1_TYPE_INTEGER,
				     &parsed);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (ctx->version != 0x02) {
		ret = X509_PARSER_ERROR_VERSION_NOT_3;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += parsed;
	cur_off += parsed;
	remain -= parsed;

	ret = parse_x509_tbsCert_sig_AlgorithmIdentifier(ctx, cert, cur_off, remain,
							 &alg, &parsed);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += parsed;
	cur_off += parsed;
	remain -= parsed;

	ret = parse_x509_Name(buf, remain, &parsed, &empty_issuer);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	ctx->issuer_start = cur_off;
	ctx->issuer_len = parsed;

	if (empty_issuer) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	issuer_ptr = buf;
	issuer_len = parsed;

	buf += parsed;
	cur_off += parsed;
	remain -= parsed;

	ret = parse_x509_Validity(ctx, cert, cur_off, remain, &parsed);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += parsed;
	cur_off += parsed;
	remain -= parsed;

	ret = parse_x509_Name(buf, remain, &parsed, &ctx->empty_subject);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->subject_start = cur_off;
	ctx->subject_len = parsed;

	subject_ptr = buf;
	subject_len = parsed;

	buf += parsed;
	cur_off += parsed;
	remain -= parsed;

	ctx->subject_issuer_identical = 0;
	if (subject_len == issuer_len) {
		ctx->subject_issuer_identical = !bufs_differ(subject_ptr,
							     issuer_ptr,
							     issuer_len);
	}

	ret = parse_x509_subjectPublicKeyInfo(ctx, cert, cur_off, remain, &parsed);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += parsed;
	cur_off += parsed;
	remain -= parsed;

	if (remain) {
		ret = parse_x509_cert_Extensions(ctx, cert, cur_off, remain,
						 &parsed);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += parsed;
		cur_off += parsed;
		remain -= parsed;
	}

	if (remain != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

#ifndef TEMPORARY_LAXIST_CA_WO_SKI
	if (ctx->ca_true && !ctx->has_ski) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
#endif

	if (ctx->keyCertSign_set && (!ctx->ca_true || !ctx->bc_critical)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (ctx->cRLSign_set && ctx->empty_subject) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (ctx->pathLenConstraint_set &&
	    (!ctx->ca_true || !ctx->keyCertSign_set)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (ctx->has_name_constraints && !ctx->ca_true) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (ctx->ca_true && ctx->has_crldp && !ctx->one_crldp_has_all_reasons) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = tbs_cert_len;

	*sig_alg = alg;

	ret = 0;

out:
	return ret;
}

static int parse_x509_signatureAlgorithm(cert_parsing_ctx *ctx,
					 const x509_u8 *cert, x509_u32 off, x509_u32 len,
					 x509_u32 *eaten)
{
	x509_u32 prev_len;
	int ret;

	if ((cert == NULL) || (len == 0) || (ctx == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	prev_len = ctx->tbs_sig_alg_len;
	if (prev_len > len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = bufs_differ(cert + ctx->tbs_sig_alg_start, cert + off, prev_len);
	if (ret) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	ctx->sig_alg_start = off;
	ctx->sig_alg_len = prev_len;

	*eaten = prev_len;

	ret = 0;

out:
	return ret;
}

static int parse_x509_signatureValue(cert_parsing_ctx *ctx,
				     const x509_u8 *cert, x509_u32 off, x509_u32 len,
				     const _sig_alg *sig_alg, x509_u32 *eaten)
{
	x509_u32 saved_off = off;
	sig_params *params;
	int ret;

	if ((cert == NULL) || (len == 0) || (sig_alg == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (sig_alg->parse_sig == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	params = &(ctx->sig_alg_params);

	ret = sig_alg->parse_sig(params, cert, off, len, eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->sig_start = saved_off;
	ctx->sig_len = *eaten;
	ret = 0;

out:
	return ret;
}

static cert_parsing_ctx get_zeroized_cert_ctx_val(void)
{
	cert_parsing_ctx zeroized_ctx = { 0 };

	return zeroized_ctx;
}

int parse_x509_cert(cert_parsing_ctx *ctx, const x509_u8 *cert, x509_u32 len)
{
	x509_u32 seq_data_len = 0;
	x509_u32 eaten = 0;
	x509_u32 off = 0;
	const _sig_alg *sig_alg = NULL;
	int ret;

	if ((cert == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*ctx = get_zeroized_cert_ctx_val();

	ret = parse_id_len(cert, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &eaten, &seq_data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	len -= eaten;
	off += eaten;

	if (seq_data_len != len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_x509_tbsCertificate(ctx, cert, off, len, &sig_alg, &eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->tbs_start = off;
	ctx->tbs_len = eaten;

	len -= eaten;
	off += eaten;

	ret = parse_x509_signatureAlgorithm(ctx, cert, off, len, &eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	len -= eaten;
	off += eaten;

	ret = parse_x509_signatureValue(ctx, cert, off, len, sig_alg, &eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len != eaten) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

#undef X509_FILE_NUM
#define X509_FILE_NUM 3 

#define DECL_HASH_ALG(TTalg, UUname, VVoid, WWoidbuf, XXtype) \
static const x509_u8 _##TTalg##_hash_name[] = UUname;             \
static const x509_u8 _##TTalg##_hash_printable_oid[] = VVoid;     \
static const x509_u8 _##TTalg##_hash_der_oid[] = WWoidbuf;        \
							     \
static const _hash_alg _##TTalg##_hash_alg = {               \
	.alg_name = _##TTalg##_hash_name,                    \
	.alg_printable_oid = _##TTalg##_hash_printable_oid,  \
	.alg_der_oid = _##TTalg##_hash_der_oid,              \
	.alg_der_oid_len = sizeof(_##TTalg##_hash_der_oid),  \
	.hash_id = (XXtype),				     \
}

DECL_HASH_ALG(md2, "MD2", "1.2.840.113549.2.2", P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x02, 0x02 }), HASH_ALG_MD2);
DECL_HASH_ALG(md4, "MD4", "1.2.840.113549.2.4", P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x02, 0x04 }), HASH_ALG_MD4);
DECL_HASH_ALG(md5, "MD5", "1.2.840.113549.2.5", P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x02, 0x05 }), HASH_ALG_MD5);
DECL_HASH_ALG(mdc2, "MDC2", "1.3.14.3.2.19", P99_PROTECT({ 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x13 }), HASH_ALG_MDC2);
DECL_HASH_ALG(sha1, "SHA1", "1.3.14.3.2.26", P99_PROTECT({ 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1a }), HASH_ALG_SHA1);
DECL_HASH_ALG(ripemd160, "RIPEMD160", "1.3.36.3.2.1", P99_PROTECT({ 0x06, 0x05, 0x2b, 0x24, 0x03, 0x02, 0x01 }), HASH_ALG_RIPEMD160);
DECL_HASH_ALG(ripemd160_iso, "RIPEMD160", "1.0.10118.3.49", P99_PROTECT({ 0x06, 0x06, 0x28, 0xCF, 0x06, 0x03, 0x00, 0x31 }), HASH_ALG_RIPEMD160);
DECL_HASH_ALG(ripemd128, "RIPEMD128", "1.3.36.3.2.2", P99_PROTECT({ 0x06, 0x05, 0x2b, 0x24, 0x03, 0x02, 0x02 }), HASH_ALG_RIPEMD128);
DECL_HASH_ALG(ripemd128_iso, "RIPEMD128", "1.0.10118.3.50", P99_PROTECT({ 0x06, 0x06, 0x28, 0xCF, 0x06, 0x03, 0x00, 0x32 }), HASH_ALG_RIPEMD128);
DECL_HASH_ALG(ripemd256, "RIPEMD256", "1.3.36.3.2.3", P99_PROTECT({ 0x06, 0x05, 0x2b, 0x24, 0x03, 0x02, 0x03 }), HASH_ALG_RIPEMD256);
DECL_HASH_ALG(sha224, "SHA224", "2.16.840.1.101.3.4.2.4", P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x04 }), HASH_ALG_SHA224);
DECL_HASH_ALG(sha256, "SHA256", "2.16.840.1.101.3.4.2.1", P99_PROTECT({  0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01 }), HASH_ALG_SHA256);
DECL_HASH_ALG(sha384, "SHA384", "2.16.840.1.101.3.4.2.2", P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02 }), HASH_ALG_SHA384);
DECL_HASH_ALG(sha512, "SHA512", "2.16.840.1.101.3.4.2.3", P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03 }), HASH_ALG_SHA512);
DECL_HASH_ALG(sha512_224, "SHA512_224", "2.16.840.1.101.3.4.2.5", P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x05 }), HASH_ALG_SHA512_224);
DECL_HASH_ALG(sha512_256, "SHA512_256", "2.16.840.1.101.3.4.2.6", P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03,0x04, 0x02, 0x06 }), HASH_ALG_SHA512_256);
DECL_HASH_ALG(sha3_224, "SHA3_224", "2.16.840.1.101.3.4.2.7", P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x07 }), HASH_ALG_SHA3_224);
DECL_HASH_ALG(sha3_256, "SHA3_256", "2.16.840.1.101.3.4.2.8", P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x08 }), HASH_ALG_SHA3_256);
DECL_HASH_ALG(sha3_384, "SHA3_384", "2.16.840.1.101.3.4.2.9", P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x09 }), HASH_ALG_SHA3_384);
DECL_HASH_ALG(sha3_512, "SHA3_512", "2.16.840.1.101.3.4.2.10", P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x0a }), HASH_ALG_SHA3_512);
DECL_HASH_ALG(shake128, "SHAKE128", "2.16.840.1.101.3.4.2.11", P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x0b }), HASH_ALG_SHAKE128);
DECL_HASH_ALG(shake256, "SHAKE256", "2.16.840.1.101.3.4.2.12", P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x0c }), HASH_ALG_SHAKE256);
DECL_HASH_ALG(hbelt, "HBELT", "1.2.112.0.2.0.34.101.31.81", P99_PROTECT({ 0x06, 0x09, 0x2A, 0x70, 0x00, 0x02, 0x00, 0x22, 0x65, 0x1F, 0x51 }), HASH_ALG_HBELT);
DECL_HASH_ALG(bash256, "BASH256", "1.2.112.0.2.0.34.101.77.11", P99_PROTECT({ 0x06, 0x09, 0x2A, 0x70, 0x00, 0x02, 0x00, 0x22, 0x65, 0x4D, 0x0B }), HASH_ALG_BASH256);
DECL_HASH_ALG(bash384, "BASH384", "1.2.112.0.2.0.34.101.77.12", P99_PROTECT({ 0x06, 0x09, 0x2A, 0x70, 0x00, 0x02, 0x00, 0x22, 0x65, 0x4D, 0x0C }), HASH_ALG_BASH384);
DECL_HASH_ALG(bash512, "BASH512", "1.2.112.0.2.0.34.101.77.13", P99_PROTECT({ 0x06, 0x09, 0x2A, 0x70, 0x00, 0x02, 0x00, 0x22, 0x65, 0x4D, 0x0D }), HASH_ALG_BASH512);
DECL_HASH_ALG(whirlpool, "WHIRLPOOL", "1.0.10118.3.55", P99_PROTECT({ 0x06, 0x06, 0x28, 0xCF, 0x06, 0x03, 0x00, 0x37 }), HASH_ALG_WHIRLPOOL);
DECL_HASH_ALG(streebog256, "STREEBOG256", "1.2.643.7.1.1.2.2", P99_PROTECT({ 0x06, 0x08, 0x2a, 0x85, 0x03, 0x07, 0x01, 0x01, 0x02, 0x02 }), HASH_ALG_STREEBOG256);
DECL_HASH_ALG(streebog256_bis, "STREEBOG256", "1.0.10118.3.60", P99_PROTECT({ 0x06, 0x06, 0x28, 0xCF, 0x06, 0x03, 0x00, 0x3c }), HASH_ALG_STREEBOG256);
DECL_HASH_ALG(streebog512, "STREEBOG512", "1.2.643.7.1.1.2.3", P99_PROTECT({ 0x06, 0x08, 0x2a, 0x85, 0x03, 0x07, 0x01, 0x01, 0x02, 0x03 }), HASH_ALG_STREEBOG512);
DECL_HASH_ALG(streebog512_bis, "STREEBOG512", "1.0.10118.3.59", P99_PROTECT({ 0x06, 0x06, 0x28, 0xCF, 0x06, 0x03, 0x00, 0x3b}), HASH_ALG_STREEBOG512);
DECL_HASH_ALG(sm3, "SM3", "1.0.10118.3.65", P99_PROTECT({ 0x06, 0x06, 0x28, 0xCF, 0x06, 0x03, 0x00, 0x41 }), HASH_ALG_SM3);
DECL_HASH_ALG(gostR3411_94, "GOST R 34.11-94", "1.2.643.2.2.9", P99_PROTECT({ 0x06, 0x06, 0x2a, 0x85, 0x03, 0x02, 0x02, 0x09 }), HASH_ALG_GOSTR3411_94);
DECL_HASH_ALG(gostR3411_94_bis, "GOST R 34.11-94", "1.2.643.2.2.30.1", P99_PROTECT({ 0x06, 0x07, 0x2a, 0x85, 0x03, 0x02, 0x02, 0x1e, 0x01 }), HASH_ALG_GOSTR3411_94); 

const _hash_alg *known_hashes[] = {
	&_md2_hash_alg,
	&_md4_hash_alg,
	&_md5_hash_alg,
	&_mdc2_hash_alg,
	&_sha1_hash_alg,
	&_ripemd160_hash_alg,
	&_ripemd160_iso_hash_alg,
	&_ripemd128_hash_alg,
	&_ripemd128_iso_hash_alg,
	&_ripemd256_hash_alg,
	&_sha224_hash_alg,
	&_sha256_hash_alg,
	&_sha384_hash_alg,
	&_sha512_hash_alg,
	&_sha512_224_hash_alg,
	&_sha512_256_hash_alg,
	&_sha3_224_hash_alg,
	&_sha3_256_hash_alg,
	&_sha3_384_hash_alg,
	&_sha3_512_hash_alg,
	&_shake128_hash_alg,
	&_shake256_hash_alg,
	&_hbelt_hash_alg,
	&_bash256_hash_alg,
	&_bash384_hash_alg,
	&_bash512_hash_alg,
	&_whirlpool_hash_alg,
	&_gostR3411_94_hash_alg,
	&_gostR3411_94_bis_hash_alg,
	&_streebog256_hash_alg,
	&_streebog256_bis_hash_alg,
	&_streebog512_hash_alg,
	&_streebog512_bis_hash_alg,
	&_sm3_hash_alg,
};

#define NUM_KNOWN_HASHES (sizeof(known_hashes) / sizeof(known_hashes[0]))

static const x509_u8 _mgf_alg_mgf1_name[] = "MGF1";
static const x509_u8 _mgf_alg_mgf1_printable_oid[] = "1.2.840.113549.1.1.8";
static const x509_u8 _mgf_alg_mgf1_der_oid[] = { 0x06, 0x09, 0x2a, 0x86,
					    0x48, 0x86, 0xf7, 0x0d,
					    0x01, 0x01, 0x08 };

static const _mgf _mgf1_alg = {
	.alg_name = _mgf_alg_mgf1_name,
	.alg_printable_oid = _mgf_alg_mgf1_printable_oid,
	.alg_der_oid = _mgf_alg_mgf1_der_oid,
	.alg_der_oid_len = sizeof(_mgf_alg_mgf1_der_oid),
	.mgf_id = MGF_ALG_MGF1
};

#define DECL_CURVE(TTcurve, UUname, VVoid, WWoidbuf, XXtype, YYbitlen)    \
static const x509_u8 _##TTcurve##_curve_name[] = UUname;                       \
static const x509_u8 _##TTcurve##_curve_printable_oid[] = VVoid;               \
static const x509_u8 _##TTcurve##_curve_der_oid[] = WWoidbuf;                  \
									  \
static const _curve _curve_##TTcurve = {                                  \
	.crv_name = _##TTcurve##_curve_name,                              \
	.crv_printable_oid = _##TTcurve##_curve_printable_oid,            \
	.crv_der_oid = _##TTcurve##_curve_der_oid,                        \
	.crv_der_oid_len = sizeof(_##TTcurve##_curve_der_oid),            \
	.crv_order_bit_len = (YYbitlen),				  \
	.crv_id = (XXtype),						  \
}

DECL_CURVE(Curve25519, "Curve25519", "1.3.6.1.4.1.11591.15.1", P99_PROTECT({ 0x06, 0x09, 0x2B, 0x06, 0x01, 0x04, 0x01, 0xDA, 0x47, 0x0F, 0x01 }), CURVE_WEI25519, 256);
DECL_CURVE(Curve448, "Curve448", "1.3.6.1.4.1.11591.15.2", P99_PROTECT({ 0x06, 0x09, 0x2B, 0x06, 0x01, 0x04, 0x01, 0xDA, 0x47, 0x0F, 0x02 }), CURVE_WEI448, 448);
DECL_CURVE(bign_curve256v1, "bign-curve256v1", "1.2.112.0.2.0.34.101.45.3.1", P99_PROTECT({ 0x06, 0x0a, 0x2a, 0x70, 0x00, 0x02, 0x00, 0x22, 0x65, 0x2d, 0x03, 0x01 }), CURVE_BIGN256v1, 256);
DECL_CURVE(bign_curve384v1, "bign-curve384v1", "1.2.112.0.2.0.34.101.45.3.2", P99_PROTECT({ 0x06, 0x0a, 0x2a, 0x70, 0x00, 0x02, 0x00, 0x22, 0x65, 0x2d, 0x03, 0x02 }), CURVE_BIGN384v1, 384);
DECL_CURVE(bign_curve512v1, "bign-curve512v1", "1.2.112.0.2.0.34.101.45.3.3", P99_PROTECT({ 0x06, 0x0a, 0x2a, 0x70, 0x00, 0x02, 0x00, 0x22, 0x65, 0x2d, 0x03, 0x03 }), CURVE_BIGN512v1, 512);
DECL_CURVE(prime192v1, "prime192v1", "1.2.840.10045.3.0.1", P99_PROTECT({ 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x01 }), CURVE_SECP192R1, 192);
DECL_CURVE(c2pnb163v1, "c2pnb163v1", "1.2.840.10045.3.0.1", P99_PROTECT({ 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x00, 0x01 }), CURVE_C2PNB163V1, 163);
DECL_CURVE(sect571k1, "sect571k1", "1.3.132.0.38", P99_PROTECT({ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x26 }), CURVE_SECT571K1, 571);
DECL_CURVE(sect163k1, "sect163k1", "1.3.132.0.1", P99_PROTECT({ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x01 }), CURVE_SECT163K1, 163);
DECL_CURVE(secp192k1, "secp192k1", "1.3.132.0.31", P99_PROTECT({ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x1f }), CURVE_SECP192K1, 192);
DECL_CURVE(secp224k1, "secp224k1", "1.3.132.0.32", P99_PROTECT({ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x20 }), CURVE_SECP224K1, 224);
DECL_CURVE(secp256k1, "secp256k1", "1.3.132.0.10", P99_PROTECT({ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x0a }), CURVE_SECP256K1, 256);
DECL_CURVE(secp224r1, "secp224r1", "1.3.132.0.33", P99_PROTECT({ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x21 }), CURVE_SECP224R1, 224);
DECL_CURVE(secp256r1, "secp256r1", "1.2.840.10045.3.1.7", P99_PROTECT({ 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07 }), CURVE_SECP256R1, 256);
DECL_CURVE(secp384r1, "secp384r1", "1.3.132.0.34", P99_PROTECT({ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x22 }), CURVE_SECP384R1, 384);
DECL_CURVE(secp521r1, "secp521r1", "1.3.132.0.35", P99_PROTECT({ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x23 }), CURVE_SECP521R1, 521);
DECL_CURVE(brainpoolP192R1, "brainpoolP192R1", "1.3.36.3.3.2.8.1.1.3", P99_PROTECT({ 0x06, 0x09, 0x2b, 0x24, 0x03, 0x03, 0x02, 0x08, 0x01, 0x01, 0x03 }), CURVE_BRAINPOOLP192R1, 192);
DECL_CURVE(brainpoolP224R1, "brainpoolP224R1", "1.3.36.3.3.2.8.1.1.5", P99_PROTECT({ 0x06, 0x09, 0x2b, 0x24, 0x03, 0x03, 0x02, 0x08, 0x01, 0x01, 0x05 }), CURVE_BRAINPOOLP224R1, 224);
DECL_CURVE(brainpoolP256R1, "brainpoolP256R1", "1.3.36.3.3.2.8.1.1.7", P99_PROTECT({ 0x06, 0x09, 0x2b, 0x24, 0x03, 0x03, 0x02, 0x08, 0x01, 0x01, 0x07 }), CURVE_BRAINPOOLP256R1, 256);
DECL_CURVE(brainpoolP320R1, "brainpoolP320R1", "1.3.36.3.3.2.8.1.1.9", P99_PROTECT({ 0x06, 0x09, 0x2b, 0x24, 0x03, 0x03, 0x02, 0x08, 0x01, 0x01, 0x09 }), CURVE_BRAINPOOLP320R1, 320);
DECL_CURVE(brainpoolP384R1, "brainpoolP384R1", "1.3.36.3.3.2.8.1.1.11", P99_PROTECT({ 0x06, 0x09, 0x2b, 0x24, 0x03, 0x03, 0x02, 0x08,0x01, 0x01, 0x0b }), CURVE_BRAINPOOLP384R1, 384);
DECL_CURVE(brainpoolP512R1, "brainpoolP512R1", "1.3.36.3.3.2.8.1.1.13", P99_PROTECT({ 0x06, 0x09, 0x2b, 0x24, 0x03, 0x03, 0x02, 0x08, 0x01, 0x01, 0x0d }), CURVE_BRAINPOOLP512R1, 512);
DECL_CURVE(brainpoolP192T1, "brainpoolP192T1", "1.3.36.3.3.2.8.1.1.4", P99_PROTECT({ 0x06, 0x09, 0x2b, 0x24, 0x03, 0x03, 0x02, 0x08, 0x01, 0x01, 0x04 }), CURVE_BRAINPOOLP192T1, 192);
DECL_CURVE(brainpoolP224T1, "brainpoolP224T1", "1.3.36.3.3.2.8.1.1.6", P99_PROTECT({ 0x06, 0x09, 0x2b, 0x24, 0x03, 0x03, 0x02, 0x08, 0x01, 0x01, 0x06 }), CURVE_BRAINPOOLP224T1, 224);
DECL_CURVE(brainpoolP256T1, "brainpoolP256T1", "1.3.36.3.3.2.8.1.1.8", P99_PROTECT({ 0x06, 0x09, 0x2b, 0x24, 0x03, 0x03, 0x02, 0x08, 0x01, 0x01, 0x08 }), CURVE_BRAINPOOLP256T1, 256);
DECL_CURVE(brainpoolP320T1, "brainpoolP320T1", "1.3.36.3.3.2.8.1.1.10", P99_PROTECT({ 0x06, 0x09, 0x2b, 0x24, 0x03, 0x03, 0x02, 0x08, 0x01, 0x01, 0x0a }), CURVE_BRAINPOOLP320T1, 320);
DECL_CURVE(brainpoolP384T1, "brainpoolP384T1", "1.3.36.3.3.2.8.1.1.12", P99_PROTECT({ 0x06, 0x09, 0x2b, 0x24, 0x03, 0x03, 0x02, 0x08, 0x01, 0x01, 0x0c }), CURVE_BRAINPOOLP384T1, 384);
DECL_CURVE(brainpoolP512T1, "brainpoolP512T1", "1.3.36.3.3.2.8.1.1.14", P99_PROTECT({ 0x06, 0x09, 0x2b, 0x24, 0x03, 0x03, 0x02, 0x08, 0x01, 0x01, 0x0e }), CURVE_BRAINPOOLP512T1, 512);
DECL_CURVE(sm2p256v1, "sm2p256v1", "1.2.156.10197.1.301", P99_PROTECT({ 0x06, 0x08, 0x2a, 0x81, 0x1c, 0xcf, 0x55, 0x01, 0x82, 0x2d }), CURVE_SM2P256V1, 256);
DECL_CURVE(frp256v1, "frp256v1", "1.2.250.1.223.101.256.1", P99_PROTECT({ 0x06, 0x0A, 0x2A, 0x81, 0x7A, 0x01, 0x81, 0x5F, 0x65, 0x82, 0x00, 0x01 }), CURVE_FRP256V1, 256);
DECL_CURVE(gost_R3410_2001_CryptoPro_A_ParamSet, "gost_R3410_2001_CryptoPro_A_ParamSet", "1.2.643.2.2.35.1", P99_PROTECT({ 0x06, 0x07, 0x2A, 0x85, 0x03, 0x02, 0x02, 0x23, 0x01 }), CURVE_GOST_R3410_2001_CRYPTOPRO_A_PARAMSET, 256);
DECL_CURVE(gost_R3410_2001_CryptoPro_B_ParamSet, "gost_R3410_2001_CryptoPro_B_ParamSet", "1.2.643.2.2.35.2", P99_PROTECT({ 0x06, 0x07, 0x2A, 0x85, 0x03, 0x02, 0x02, 0x23, 0x02 }), CURVE_GOST_R3410_2001_CRYPTOPRO_B_PARAMSET, 256);
DECL_CURVE(gost_R3410_2001_CryptoPro_C_ParamSet, "gost_R3410_2001_CryptoPro_C_ParamSet", "1.2.643.2.2.35.3", P99_PROTECT({ 0x06, 0x07, 0x2A, 0x85, 0x03, 0x02, 0x02, 0x23, 0x03 }), CURVE_GOST_R3410_2001_CRYPTOPRO_C_PARAMSET, 256);
DECL_CURVE(gost_R3410_2001_CryptoPro_XchA_ParamSet, "gost_R3410_2001_CryptoPro_XchA_ParamSet", "1.2.643.2.2.36.0", P99_PROTECT({ 0x06, 0x07, 0x2A, 0x85, 0x03, 0x02, 0x02, 0x24, 0x00 }), CURVE_GOST_R3410_2001_CRYPTOPRO_XCHA_PARAMSET, 256);
DECL_CURVE(gost_R3410_2001_CryptoPro_XchB_ParamSet, "gost_R3410_2001_CryptoPro_XchB_ParamSet", "1.2.643.2.2.36.1", P99_PROTECT({ 0x06, 0x07, 0x2A, 0x85, 0x03, 0x02, 0x02, 0x24, 0x01 }), CURVE_GOST_R3410_2001_CRYPTOPRO_XCHB_PARAMSET, 256);
DECL_CURVE(gost_R3410_2001_TestParamSet, "gost_R3410_2001_TestParamSet", "1.2.643.2.2.35.0", P99_PROTECT({ 0x06, 0x07, 0x2A, 0x85, 0x03, 0x02, 0x02, 0x23, 0x00 }), CURVE_GOST_R3410_2001_TESTPARAMSET, 256);
DECL_CURVE(gost_R3410_2012_256_paramSetA, "gost_R3410_2012_256_paramSetA", "1.2.643.7.1.2.1.1.1", P99_PROTECT({ 0x06, 0x09, 0x2A, 0x85, 0x03, 0x07, 0x01, 0x02, 0x01, 0x01, 0x01 }), CURVE_GOST_R3410_2012_256_PARAMSETA, 257);
DECL_CURVE(gost_R3410_2012_256_paramSetB, "gost_R3410_2012_256_paramSetB", "1.2.643.7.1.2.1.1.2", P99_PROTECT({ 0x06, 0x09, 0x2A, 0x85, 0x03, 0x07, 0x01, 0x02, 0x01, 0x01, 0x02 }), CURVE_GOST_R3410_2012_256_PARAMSETB, 256);
DECL_CURVE(gost_R3410_2012_256_paramSetC, "gost_R3410_2012_256_paramSetC", "1.2.643.7.1.2.1.1.3", P99_PROTECT({ 0x06, 0x09, 0x2A, 0x85, 0x03, 0x07, 0x01, 0x02, 0x01, 0x01, 0x02 }), CURVE_GOST_R3410_2012_256_PARAMSETC, 256);
DECL_CURVE(gost_R3410_2012_256_paramSetD, "gost_R3410_2012_256_paramSetD", "1.2.643.7.1.2.1.1.4", P99_PROTECT({ 0x06, 0x09, 0x2A, 0x85, 0x03, 0x07, 0x01, 0x02, 0x01, 0x01, 0x04 }), CURVE_GOST_R3410_2012_256_PARAMSETD, 256);
DECL_CURVE(gost_R3410_2012_512_paramSetA, "gost_R3410_2012_512_paramSetA", "1.2.643.7.1.2.1.2.1", P99_PROTECT({ 0x06, 0x09, 0x2A, 0x85, 0x03, 0x07, 0x01, 0x02, 0x01, 0x02, 0x01 }), CURVE_GOST_R3410_2012_512_PARAMSETA, 512);
DECL_CURVE(gost_R3410_2012_512_paramSetB, "gost_R3410_2012_512_paramSetB", "1.2.643.7.1.2.1.2.2", P99_PROTECT({ 0x06, 0x09, 0x2A, 0x85, 0x03, 0x07, 0x01, 0x02, 0x01, 0x02, 0x02 }), CURVE_GOST_R3410_2012_512_PARAMSETB, 512);
DECL_CURVE(gost_R3410_2012_512_paramSetC, "gost_R3410_2012_512_paramSetC", "1.2.643.7.1.2.1.2.3", P99_PROTECT({ 0x06, 0x09, 0x2A, 0x85, 0x03, 0x07, 0x01, 0x02, 0x01, 0x02, 0x03 }), CURVE_GOST_R3410_2012_512_PARAMSETC, 512);
DECL_CURVE(gost_R3410_2012_512_paramSetTest, "gost_R3410_2012_512_paramSetTest",  "1.2.643.7.1.2.1.2.0", P99_PROTECT({ 0x06, 0x09, 0x2A, 0x85, 0x03, 0x07, 0x01, 0x02, 0x01, 0x02, 0x00 }), CURVE_GOST_R3410_2012_512_PARAMSETTEST, 511);

const _curve *known_curves[] = {
	&_curve_secp224r1,
	&_curve_secp256r1,
	&_curve_secp384r1,
	&_curve_secp521r1,
	&_curve_prime192v1,
	&_curve_c2pnb163v1,
	&_curve_sect571k1,
	&_curve_sect163k1,
	&_curve_secp192k1,
	&_curve_secp224k1,
	&_curve_secp256k1,

	&_curve_brainpoolP192R1,
	&_curve_brainpoolP224R1,
	&_curve_brainpoolP256R1,
	&_curve_brainpoolP320R1,
	&_curve_brainpoolP384R1,
	&_curve_brainpoolP512R1,
	&_curve_brainpoolP192T1,
	&_curve_brainpoolP224T1,
	&_curve_brainpoolP256T1,
	&_curve_brainpoolP320T1,
	&_curve_brainpoolP384T1,
	&_curve_brainpoolP512T1,

	&_curve_sm2p256v1,

	&_curve_bign_curve256v1,
	&_curve_bign_curve384v1,
	&_curve_bign_curve512v1,

	&_curve_frp256v1,

	&_curve_gost_R3410_2001_CryptoPro_A_ParamSet,
	&_curve_gost_R3410_2001_CryptoPro_B_ParamSet,
	&_curve_gost_R3410_2001_CryptoPro_C_ParamSet,
	&_curve_gost_R3410_2001_CryptoPro_XchA_ParamSet,
	&_curve_gost_R3410_2001_CryptoPro_XchB_ParamSet,
	&_curve_gost_R3410_2001_TestParamSet,
	&_curve_gost_R3410_2012_256_paramSetA,
	&_curve_gost_R3410_2012_256_paramSetB,
	&_curve_gost_R3410_2012_256_paramSetC,
	&_curve_gost_R3410_2012_256_paramSetD,
	&_curve_gost_R3410_2012_512_paramSetA,
	&_curve_gost_R3410_2012_512_paramSetB,
	&_curve_gost_R3410_2012_512_paramSetC,
	&_curve_gost_R3410_2012_512_paramSetTest,

	&_curve_Curve25519,
	&_curve_Curve448,
};

#define NUM_KNOWN_CURVES (sizeof(known_curves) / sizeof(known_curves[0]))

#define DECL_SIG_ALG(TTalg, SSsig, HHhash, YYparse_sig, ZZparse_algoid, UUname, VVoid, WWoidbuf) \
static const x509_u8 _##TTalg##_sig_name[] = UUname;             \
static const x509_u8 _##TTalg##_sig_printable_oid[] = VVoid;     \
static const x509_u8 _##TTalg##_sig_der_oid[] = WWoidbuf;        \
							    \
static const _sig_alg _##TTalg##_sig_alg = {                \
	.alg_name = _##TTalg##_sig_name,                    \
	.alg_printable_oid = _##TTalg##_sig_printable_oid,  \
	.alg_der_oid = _##TTalg##_sig_der_oid,              \
	.alg_der_oid_len = sizeof(_##TTalg##_sig_der_oid),  \
	.sig_id = (SSsig),				    \
	.hash_id = (HHhash),				    \
	.parse_sig = (YYparse_sig),			    \
	.parse_algoid_sig_params = (ZZparse_algoid),	    \
}

DECL_SIG_ALG(ecdsa_sha1              , SIG_ALG_ECDSA             , HASH_ALG_SHA1        , parse_sig_ecdsa         , parse_algoid_sig_params_ecdsa_with          , "ecdsa-with-SHA1"                             , "1.2.840.10045.4.1"         , P99_PROTECT({ 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x01 }));
DECL_SIG_ALG(ecdsa_sha224            , SIG_ALG_ECDSA             , HASH_ALG_SHA224      , parse_sig_ecdsa         , parse_algoid_sig_params_ecdsa_with          , "ecdsa-with-SHA224"                           , "1.2.840.10045.4.3.1"       , P99_PROTECT({ 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x01 }));
DECL_SIG_ALG(ecdsa_sha256            , SIG_ALG_ECDSA             , HASH_ALG_SHA256      , parse_sig_ecdsa         , parse_algoid_sig_params_ecdsa_with          , "ecdsa-with-SHA256"                           , "1.2.840.10045.4.3.2"       , P99_PROTECT({ 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02 }));
DECL_SIG_ALG(ecdsa_sha384            , SIG_ALG_ECDSA             , HASH_ALG_SHA384      , parse_sig_ecdsa         , parse_algoid_sig_params_ecdsa_with          , "ecdsa-with-SHA384"                           , "1.2.840.10045.4.3.3"       , P99_PROTECT({ 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x03 }));
DECL_SIG_ALG(ecdsa_sha512            , SIG_ALG_ECDSA             , HASH_ALG_SHA512      , parse_sig_ecdsa         , parse_algoid_sig_params_ecdsa_with          , "ecdsa-with-SHA512"                           , "1.2.840.10045.4.3.4"       , P99_PROTECT({ 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x04 }));
DECL_SIG_ALG(ecdsa_with_sha3_256     , SIG_ALG_ECDSA             , HASH_ALG_SHA3_256    , parse_sig_ecdsa         , parse_algoid_sig_params_ecdsa_with          , "id-ecdsa-with-sha3-256"                      , "2.16.840.1.101.3.4.3.10"   , P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x0a }));
DECL_SIG_ALG(ecdsa_with_shake128     , SIG_ALG_ECDSA             , HASH_ALG_SHAKE128    , parse_sig_ecdsa         , parse_algoid_sig_params_ecdsa_with          , "ecdsa-with-shake128"                         , "1.3.6.1.5.5.7.6.32"        , P99_PROTECT({ 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x06, 0x20 })); 
DECL_SIG_ALG(ecdsa_with_shake256     , SIG_ALG_ECDSA             , HASH_ALG_SHAKE256    , parse_sig_ecdsa         , parse_algoid_sig_params_ecdsa_with          , "ecdsa-with-shake256"                         , "1.3.6.1.5.5.7.6.32"        , P99_PROTECT({ 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x06, 0x20 })); 
DECL_SIG_ALG(ecdsa_with_specified    , SIG_ALG_ECDSA             , HASH_ALG_UNKNOWN     , parse_sig_ecdsa         , parse_algoid_sig_params_ecdsa_with_specified, "ecdsa-with-specified"                        , "1.2.840.10045.4.3"         , P99_PROTECT({ 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03 })); 
DECL_SIG_ALG(ed25519                 , SIG_ALG_ED25519           , HASH_ALG_SHA512      , parse_sig_ed25519       , parse_algoid_sig_params_eddsa               , "Ed25519"                                     , "1.3.101.112"               , P99_PROTECT({ 0x06, 0x03, 0x2b, 0x65, 0x70 }));
DECL_SIG_ALG(ed448                   , SIG_ALG_ED448             , HASH_ALG_SHAKE256    , parse_sig_ed448         , parse_algoid_sig_params_eddsa               , "Ed448"                                       , "1.3.101.113"               , P99_PROTECT({ 0x06, 0x03, 0x2b, 0x65, 0x71 }));
DECL_SIG_ALG(sm2_sm3                 , SIG_ALG_SM2               , HASH_ALG_SM3         , parse_sig_sm2           , parse_algoid_sig_params_sm2                 , "SM2 w/ SM3"                                  , "1.2.156.10197.1.501"       , P99_PROTECT({ 0x06, 0x08, 0x2a, 0x81, 0x1c, 0xcf, 0x55, 0x01, 0x83, 0x75 }));
DECL_SIG_ALG(rsa_mdc2                , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_MDC2        , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "mdc2WithRSA"                                 , "2.5.8.3.100"               , P99_PROTECT({ 0x06, 0x04, 0x55, 0x08, 0x03, 0x64 }));
DECL_SIG_ALG(rsa_md2                 , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_MD2         , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "md2WithRSAEncryption"                        , "1.2.840.113549.1.1.2"      , P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x02 }));
DECL_SIG_ALG(rsa_md4                 , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_MD4         , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "md4WithRSAEncryption"                        , "1.2.840.113549.1.1.3"      , P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x03 }));
DECL_SIG_ALG(rsa_md5                 , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_MD5         , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "md5WithRSAEncryption"                        , "1.2.840.113549.1.1.4"      , P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x04 }));
DECL_SIG_ALG(sha1WithRSAEnc          , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_SHA1        , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "sha1WithRSAEncryption"                       , "1.2.840.113549.1.1.5"      , P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05 }));
DECL_SIG_ALG(sha1WithRSAEnc_bis      , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_SHA1        , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "sha1WithRSAEncryption (bis)"                 , "1.3.14.3.2.29"             , P99_PROTECT({ 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1d }));
DECL_SIG_ALG(sha1WithRSAEnc_alt      , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_SHA1        , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "sha1WithRSAEncryption_alt"                   , "1.2.836.113549.1.1.5"      , P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x44, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05 })); 
DECL_SIG_ALG(sha1WithRSAEnc_alt2     , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_SHA1        , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "sha1WithRSAEncryption_alt2"                  , "1.2.4936.113549.1.1.5"     , P99_PROTECT({ 0x06, 0x09, 0x2a, 0xa6, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05 })); 
DECL_SIG_ALG(sha1WithRSAEnc_ter      , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_SHA1        , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "sha1WithRSAEncryption (ter)"                 , "1.2.840.113549.0.1.5"      , P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x00, 0x01, 0x05 })); 
DECL_SIG_ALG(rsa_sha1_crap           , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_SHA1        , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "Another unspecified RSA-SHA1 oid"            , "1.2.856.113549.1.1.5"      , P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x58, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05 })); 
DECL_SIG_ALG(rsa_sha1_crap_bis       , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_SHA1        , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "crappy sha1-with-rsa-signature"              , "1.2.872.113549.1.1.5"      , P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x68, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05 })); 
DECL_SIG_ALG(shaWithRSASig_9796_2    , SIG_ALG_RSA_9796_2_PAD    , HASH_ALG_SHA1        , parse_sig_rsa_9796_2_pad, parse_algoid_sig_params_rsa                 , "shaWithRSASignature-9796-2"                  , "1.3.14.3.2.15"             , P99_PROTECT({ 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x0f })); 
DECL_SIG_ALG(rsa_sha224              , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_SHA224      , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "sha224WithRSAEncryption"                     , "1.2.840.113549.1.1.14"     , P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0e }));
DECL_SIG_ALG(rsa_sha256              , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_SHA256      , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "sha256WithRSAEncryption"                     , "1.2.840.113549.1.1.11"     , P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b }));
DECL_SIG_ALG(rsa_sha384              , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_SHA384      , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "sha384WithRSAEncryption"                     , "1.2.840.113549.1.1.12"     , P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0c }));
DECL_SIG_ALG(rsa_sha512              , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_SHA512      , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "sha512WithRSAEncryption"                     , "1.2.840.113549.1.1.13"     , P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0d }));
DECL_SIG_ALG(rsa_ripemd160           , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_RIPEMD160   , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "rsaSignatureWithripemd160"                   , "1.3.36.3.3.1.2"            , P99_PROTECT({ 0x06, 0x06, 0x2b, 0x24, 0x03, 0x03, 0x01, 0x02 }));
DECL_SIG_ALG(rsa_ripemd128           , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_RIPEMD128   , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "rsaSignatureWithripemd128"                   , "1.3.36.3.3.1.3"            , P99_PROTECT({ 0x06, 0x06, 0x2b, 0x24, 0x03, 0x03, 0x01, 0x03 }));
DECL_SIG_ALG(rsa_ripemd256           , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_RIPEMD256   , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "rsaSignatureWithripemd256"                   , "1.3.36.3.3.1.3"            , P99_PROTECT({ 0x06, 0x06, 0x2b, 0x24, 0x03, 0x03, 0x01, 0x03 }));
DECL_SIG_ALG(pkcs1_v15_w_sha3_224    , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_SHA3_224    , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "id-rsassa-pkcs1-v1-5-with-sha3-224"          , "2.16.840.1.101.3.4.3.13"   , P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x0d }));
DECL_SIG_ALG(pkcs1_v15_w_sha3_256    , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_SHA3_256    , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "id-rsassa-pkcs1-v1-5-with-sha3-256"          , "2.16.840.1.101.3.4.3.14"   , P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x0e }));
DECL_SIG_ALG(pkcs1_v15_w_sha3_384    , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_SHA3_384    , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "id-rsassa-pkcs1-v1-5-with-sha3-384"          , "2.16.840.1.1.1.3.4.3.15"   , P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x0f,  }));
DECL_SIG_ALG(pkcs1_v15_w_sha3_512    , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_SHA3_512    , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "id-rsassa-pkcs1-v1-5-with-sha3-512"          , "2.16.840.1.1.1.3.4.3.16"   , P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x10,  }));
DECL_SIG_ALG(rsassa_pss              , SIG_ALG_RSA_SSA_PSS       , HASH_ALG_UNKNOWN     , parse_sig_rsa_ssa_pss   , parse_algoid_sig_params_rsassa_pss          , "RSASSA-PSS"                                  , "1.2.840.113549.1.1.10"     , P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0a }));
DECL_SIG_ALG(rsassa_pss_shake128     , SIG_ALG_RSA_SSA_PSS       , HASH_ALG_SHAKE128    , parse_sig_rsa_ssa_pss   , parse_algoid_sig_params_none                , "RSASSA-PSS-SHAKE128"                         , "1.3.6.1.5.5.7.6.30"        , P99_PROTECT({ 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x06, 0x1e }));  
DECL_SIG_ALG(rsassa_pss_shake256     , SIG_ALG_RSA_SSA_PSS       , HASH_ALG_SHAKE256    , parse_sig_rsa_ssa_pss   , parse_algoid_sig_params_none                , "RSASSA-PSS-SHAKE256"                         , "1.3.6.1.5.5.7.6.31"        , P99_PROTECT({ 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x06, 0x1f }));  
DECL_SIG_ALG(belgian_rsa             , SIG_ALG_BELGIAN_RSA       , HASH_ALG_UNKNOWN     , parse_sig_rsa_belgian   , parse_algoid_sig_params_rsa                 , "Undoc. Belgian RSA sig oid"                  , "2.16.56.2.1.4.1.1.3880.1"  , P99_PROTECT({ 0x06, 0x0b, 0x60, 0x38, 0x02, 0x01, 0x04, 0x01, 0x01, 0x82, 0xaf, 0x16, 0x01 })); 
DECL_SIG_ALG(rsalabs1                , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_UNKNOWN     , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "Unknown RSA Labs OID"                        , "1.2.840.113549.1.1.99"     , P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x63 }));
DECL_SIG_ALG(rsalabs2                , SIG_ALG_RSA_PKCS1_V1_5    , HASH_ALG_UNKNOWN     , parse_sig_rsa_pkcs1_v15 , parse_algoid_sig_params_rsa                 , "Unspecified RSA oid"                         , "1.2.840.113605.1.1.11"     , P99_PROTECT({ 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x45, 0x01, 0x01, 0x0b }));
DECL_SIG_ALG(dsa_sha1                , SIG_ALG_DSA               , HASH_ALG_SHA1        , parse_sig_dsa           , parse_algoid_sig_params_none                , "dsaWithSHA1"                                 , "1.3.14.3.2.27"             , P99_PROTECT({ 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1b }));
DECL_SIG_ALG(dsa_sha1_old            , SIG_ALG_DSA               , HASH_ALG_SHA1        , parse_sig_dsa           , parse_algoid_sig_params_none                , "dsaWithSHA1-old"                             , "1.3.14.3.2.12"             , P99_PROTECT({ 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x0c }));
DECL_SIG_ALG(dsa_sha1_jdk            , SIG_ALG_DSA               , HASH_ALG_SHA1        , parse_sig_dsa           , parse_algoid_sig_params_none                , "dsaWithSHA1-jdk"                             , "1.3.14.3.2.13"             , P99_PROTECT({ 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x0d })); 
DECL_SIG_ALG(dsa_sha1_bis            , SIG_ALG_DSA               , HASH_ALG_SHA1        , parse_sig_dsa           , parse_algoid_sig_params_none                , "dsa-with-sha1"                               , "1.2.840.10040.4.3"         , P99_PROTECT({ 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x38, 0x04, 0x03 }));
DECL_SIG_ALG(dsa_with_sha224         , SIG_ALG_DSA               , HASH_ALG_SHA224      , parse_sig_dsa           , parse_algoid_sig_params_none                , "id-dsa-with-sha224"                          , "2.16.840.1.101.3.4.3.1"    , P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x01 }));
DECL_SIG_ALG(dsa_with_sha256         , SIG_ALG_DSA               , HASH_ALG_SHA256      , parse_sig_dsa           , parse_algoid_sig_params_none                , "id-dsa-with-sha256"                          , "2.16.840.1.101.3.4.3.2"    , P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x02 }));
DECL_SIG_ALG(dsa_with_sha384         , SIG_ALG_DSA               , HASH_ALG_SHA384      , parse_sig_dsa           , parse_algoid_sig_params_none                , "id-dsa-with-sha384"                          , "2.16.840.1.101.3.4.3.3"    , P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x03 }));
DECL_SIG_ALG(dsa_with_sha512         , SIG_ALG_DSA               , HASH_ALG_SHA512      , parse_sig_dsa           , parse_algoid_sig_params_none                , "id-dsa-with-sha512"                          , "2.16.840.1.101.3.4.3.4"    , P99_PROTECT({ 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x04 }));
DECL_SIG_ALG(gost_R3411_94_R3410_2001, SIG_ALG_GOSTR3410_2001    , HASH_ALG_GOSTR3411_94, parse_sig_gost2001      , parse_algoid_sig_params_none                , "sig_gostR3411-94-with-gostR3410-2001"        , "1.2.643.2.2.3"             , P99_PROTECT({ 0x06, 0x06, 0x2a, 0x85, 0x03, 0x02, 0x02, 0x03 }));
DECL_SIG_ALG(gost_R3411_94_R3410_94  , SIG_ALG_GOSTR3410_94      , HASH_ALG_GOSTR3411_94, parse_sig_gost94        , parse_algoid_sig_params_none                , "sig_gostR3411-94-with-gostR3410-94"          , "1.2.643.2.2.4"             , P99_PROTECT({ 0x06, 0x06, 0x2a, 0x85, 0x03, 0x02, 0x02, 0x04 }));
DECL_SIG_ALG(gost_R3410_2012_256     , SIG_ALG_GOSTR3410_2012_256, HASH_ALG_STREEBOG256 , parse_sig_gost2012_256  , parse_algoid_sig_params_none                , "sig_gost3410-2012-256"                       , "1.2.643.7.1.1.3.2"         , P99_PROTECT({ 0x06, 0x08, 0x2a, 0x85, 0x03, 0x07, 0x01, 0x01, 0x03, 0x02 }));
DECL_SIG_ALG(gost_R3410_2012_512     , SIG_ALG_GOSTR3410_2012_512, HASH_ALG_STREEBOG512 , parse_sig_gost2012_512  , parse_algoid_sig_params_none                , "sig_gost3410-2012-512"                       , "1.2.643.7.1.1.3.3"         , P99_PROTECT({ 0x06, 0x08, 0x2a, 0x85, 0x03, 0x07, 0x01, 0x01, 0x03, 0x03 }));
DECL_SIG_ALG(bign_with_hbelt         , SIG_ALG_BIGN              , HASH_ALG_HBELT       , parse_sig_bign          , parse_algoid_sig_params_none                , "bign (STB 34.101.45-2013) using hbelt hash"  , "1.2.112.0.2.0.34.101.45.12", P99_PROTECT({ 0x06, 0x09, 0x2a, 0x70, 0x00, 0x02, 0x00, 0x22, 0x65, 0x2d, 0x0c }));
DECL_SIG_ALG(bign_with_bash256       , SIG_ALG_BIGN              , HASH_ALG_BASH256     , parse_sig_bign          , parse_algoid_sig_params_none                , "bign (STB 34.101.45-2013) using BASH256"     , "1.2.112.0.2.0.34.101.45.13", P99_PROTECT({ 0x06, 0x09, 0x2a, 0x70, 0x00, 0x02, 0x00, 0x22, 0x65, 0x2d, 0x0d }));
DECL_SIG_ALG(bign_with_bash384       , SIG_ALG_BIGN              , HASH_ALG_BASH384     , parse_sig_bign          , parse_algoid_sig_params_none                , "bign (STB 34.101.45-2013) using BASH384"     , "1.2.112.0.2.0.34.101.45.14", P99_PROTECT({ 0x06, 0x09, 0x2a, 0x70, 0x00, 0x02, 0x00, 0x22, 0x65, 0x2d, 0x0e }));
DECL_SIG_ALG(bign_with_bash512       , SIG_ALG_BIGN              , HASH_ALG_BASH512     , parse_sig_bign          , parse_algoid_sig_params_none                , "bign (STB 34.101.45-2013) using BASH512"     , "1.2.112.0.2.0.34.101.45.15", P99_PROTECT({ 0x06, 0x09, 0x2a, 0x70, 0x00, 0x02, 0x00, 0x22, 0x65, 0x2d, 0x0f }));
DECL_SIG_ALG(bign_with_hspec         , SIG_ALG_BIGN              , HASH_ALG_UNKNOWN     , parse_sig_bign          , parse_algoid_sig_params_bign_with_hspec     , "bign (STB 34.101.45-2013) w/ given hash func", "1.2.112.0.2.0.34.101.45.11", P99_PROTECT({ 0x06, 0x09, 0x2a, 0x70, 0x00, 0x02, 0x00, 0x22, 0x65, 0x2d, 0x0b }));
DECL_SIG_ALG(monkeysphere            , SIG_ALG_MONKEYSPHERE      , HASH_ALG_UNKNOWN     , parse_sig_monkey        , parse_algoid_sig_params_none                , "unknown OID from The Monkeysphere Project"   , "1.3.6.1.4.1.37210.1.1"     , P99_PROTECT({ 0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0xa2, 0x5a, 0x01, 0x01 }));

const _sig_alg *known_sig_algs[] = {
	&_ecdsa_sha1_sig_alg,
	&_ecdsa_sha224_sig_alg,
	&_ecdsa_sha256_sig_alg,
	&_ecdsa_sha384_sig_alg,
	&_ecdsa_sha512_sig_alg,
	&_ecdsa_with_sha3_256_sig_alg,
	&_ecdsa_with_shake128_sig_alg,
	&_ecdsa_with_shake256_sig_alg,

	&_rsassa_pss_sig_alg,
	&_rsassa_pss_shake128_sig_alg,
	&_rsassa_pss_shake256_sig_alg,

	&_rsa_mdc2_sig_alg,
	&_rsa_md2_sig_alg,
	&_rsa_md4_sig_alg,
	&_rsa_md5_sig_alg,
	&_rsa_sha224_sig_alg,
	&_rsa_sha256_sig_alg,
	&_rsa_sha384_sig_alg,
	&_rsa_sha512_sig_alg,
	&_sha1WithRSAEnc_sig_alg,
	&_sha1WithRSAEnc_bis_sig_alg,
	&_sha1WithRSAEnc_alt_sig_alg,
	&_sha1WithRSAEnc_alt2_sig_alg,
	&_sha1WithRSAEnc_ter_sig_alg,
	&_shaWithRSASig_9796_2_sig_alg,
	&_rsa_ripemd160_sig_alg,
	&_rsa_ripemd128_sig_alg,
	&_rsa_ripemd256_sig_alg,
	&_pkcs1_v15_w_sha3_224_sig_alg,
	&_pkcs1_v15_w_sha3_256_sig_alg,
	&_pkcs1_v15_w_sha3_384_sig_alg,
	&_pkcs1_v15_w_sha3_512_sig_alg,

	&_dsa_sha1_sig_alg,
	&_dsa_sha1_old_sig_alg,
	&_dsa_sha1_jdk_sig_alg,
	&_dsa_with_sha224_sig_alg,
	&_dsa_with_sha256_sig_alg,
	&_dsa_with_sha384_sig_alg,
	&_dsa_with_sha512_sig_alg,

	&_ed25519_sig_alg,
	&_ed448_sig_alg,

	&_sm2_sm3_sig_alg,

	&_gost_R3410_2012_256_sig_alg,
	&_gost_R3410_2012_512_sig_alg,
	&_gost_R3411_94_R3410_2001_sig_alg,
	&_gost_R3411_94_R3410_94_sig_alg,

	&_bign_with_hbelt_sig_alg,
	&_bign_with_bash256_sig_alg,
	&_bign_with_bash384_sig_alg,
	&_bign_with_bash512_sig_alg,
	&_bign_with_hspec_sig_alg,

	&_monkeysphere_sig_alg,
	&_belgian_rsa_sig_alg,
	&_rsalabs1_sig_alg,
	&_rsalabs2_sig_alg,

	&_dsa_sha1_bis_sig_alg,
	&_ecdsa_with_specified_sig_alg,
	&_rsa_sha1_crap_sig_alg,
	&_rsa_sha1_crap_bis_sig_alg,
};

const x509_u16 num_known_sig_algs = (sizeof(known_sig_algs) / sizeof(known_sig_algs[0]));

static int _extract_complex_tag(const x509_u8 *buf, x509_u32 len, x509_u32 *tag_num, x509_u32 *eaten)
{
	x509_u32 rbytes;
	x509_u32 t = 0;
	int ret;

	if ((len == 0) || (buf == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len > 4) {
		len = 4;
	}

	for (rbytes = 0; rbytes < len; rbytes++) {
		x509_u32 tmp1, tmp2;

		tmp1 = (t << (x509_u32)7);
		tmp2 = ((x509_u32)buf[rbytes] & (x509_u32)0x7f);

		t = tmp1 + tmp2;

		if ((buf[rbytes] & 0x80) == 0) {
			break;
		}
	}

	if (rbytes == len) {

		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (t < 0x1f) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*tag_num = t;
	*eaten = rbytes + 1;

	ret = 0;

out:
	return ret;
}

static int get_identifier(const x509_u8 *buf, x509_u32 len,
			  tag_class *cls, x509_u8 *prim, x509_u32 *tag_num, x509_u32 *eaten)
{
	int ret;
	x509_u32 t;
	x509_u32 rbytes = 0;
	x509_u8 p;
	tag_class c;

	if (buf == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	c = (buf[0] >> 6) & 0x03; 
	p = (buf[0] >> 5) & 0x01; 
	t = buf[0] & 0x1f;        
	rbytes = 1;

	switch (c) {
	case CLASS_UNIVERSAL:
	case CLASS_APPLICATION:
	case CLASS_CONTEXT_SPECIFIC:
	case CLASS_PRIVATE:
		break;
	default:
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
		break;
	}

	if (t == 0x1f) {
		x509_u32 tag_len = 0;

		ret = _extract_complex_tag(buf + rbytes, len - rbytes,
					   &t, &tag_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		rbytes += tag_len;
	}

	*cls = c;
	*prim = p;
	*tag_num = t;
	*eaten = rbytes;

	ret = 0;

out:
	return ret;
}

int get_length(const x509_u8 *buf, x509_u32 len, x509_u32 *adv_len, x509_u32 *eaten)
{
	x509_u32 l, rbytes = 0;
	x509_u32 len_len, b0;
	int ret;

	if (buf == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	b0 = buf[0];

	if ((b0 & 0x80) == 0) {
		l = b0 & 0x7f;

		if ((l + 1) > len) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		*eaten = 1;
		*adv_len = l;

		ret = 0;
		goto out;
	}

	if (b0 == 0x80) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	len_len = b0 & 0x7f;

	rbytes += 1;

	if ((len_len + 1) > len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	switch (len_len) {
	case 0: 

		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
		break;

	case 1: 

		l = buf[1];
		if (l <= 127) {

			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		rbytes += 1;
		break;

	case 2: 

		l = (((x509_u32)buf[1]) << 8) + buf[2];
		if (l <= 0xff) {

			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		rbytes += 2;
		break;

	case 3: 

		l = (((x509_u32)buf[1]) << 16) + (((x509_u32)buf[2]) << 8) + buf[3];
		if (l <= 0xffff) {

			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		rbytes += 3;
		break;

	case 4: 

		l = (((x509_u32)buf[1]) << 24) + (((x509_u32)buf[2]) << 16) + (((x509_u32)buf[3]) << 8) + buf[4];
		if (l <= 0xffffff) {

			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		rbytes += 4;
		break;

	default: 

		 ret = -X509_FILE_LINE_NUM_ERR;
		 ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		 goto out;
		 break;
	}

	if ((len - rbytes) < l) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = rbytes;
	*adv_len = l;

	ret = 0;

out:
	return ret;
}

int parse_id_len(const x509_u8 *buf, x509_u32 len, tag_class exp_class,
			x509_u32 exp_type, x509_u32 *parsed, x509_u32 *content_len)
{
	tag_class c = 0;
	x509_u8 p;
	x509_u32 t = 0;
	x509_u32 cur_parsed = 0;
	x509_u32 grabbed;
	x509_u32 adv_len = 0;
	int ret;

	if (buf == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = get_identifier(buf, len, &c, &p, &t, &cur_parsed);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (t != exp_type) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (c != exp_class) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	grabbed = cur_parsed;

	len -= cur_parsed;
	buf += cur_parsed;

	ret = get_length(buf, len, &adv_len, &cur_parsed);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	grabbed += cur_parsed;

	len -= cur_parsed;
	buf += cur_parsed;

	if (adv_len > len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*parsed = grabbed;

	*content_len = adv_len;

	ret = 0;

out:
	return ret;
}

int parse_explicit_id_len(const x509_u8 *buf, x509_u32 len,
				 x509_u32 exp_ext_type,
				 tag_class exp_int_class, x509_u32 exp_int_type,
				 x509_u32 *parsed, x509_u32 *data_len)
{
	x509_u32 hdr_len = 0;
	x509_u32 val_len = 0;
	int ret;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_CONTEXT_SPECIFIC,
			   exp_ext_type, &hdr_len, &val_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	len -= hdr_len;
	*parsed = hdr_len;

	ret = parse_id_len(buf, len, exp_int_class, exp_int_type,
			   &hdr_len, &val_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	len -= hdr_len;
	*parsed += hdr_len;
	if (len < val_len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*data_len = val_len;

	ret = 0;

out:
	return ret;
}

static int _parse_arc(const x509_u8 *buf, x509_u32 len, x509_u32 *arc_val, x509_u32 *eaten)
{
	x509_u32 rbytes;
	x509_u32 av = 0;
	int ret;

	if ((len == 0) || (buf == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len > 4) {
		len = 4;
	}

	for (rbytes = 0; rbytes < len; rbytes++) {
		x509_u32 tmp1, tmp2;

		tmp1 = (av << (x509_u32)7);

		tmp2 = ((x509_u32)buf[rbytes] & (x509_u32)0x7f);

		av = tmp1 + tmp2;

		if ((buf[rbytes] & 0x80) == 0) {
			break;
		}
	}

	if (rbytes >= len) {

		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*arc_val = av;
	*eaten = rbytes + 1;

	ret = 0;

out:
	return ret;
}

static const x509_u8 null_encoded_val[] = { 0x05, 0x00 };

int parse_null(const x509_u8 *buf, x509_u32 len, x509_u32 *parsed)
{
	int ret;

	if ((len == 0) || (buf == NULL) || (parsed == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len != sizeof(null_encoded_val)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = bufs_differ(buf, null_encoded_val, sizeof(null_encoded_val));
	if (ret) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;
	*parsed = sizeof(null_encoded_val);

out:
	return ret;
}

int parse_OID(const x509_u8 *buf, x509_u32 len, x509_u32 *parsed)
{
	x509_u32 data_len = 0;
	x509_u32 hdr_len = 0;
	x509_u32 remain = 0;
	x509_u32 num;
	int ret;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_OID,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	len -= hdr_len;
	buf += hdr_len;
	if (data_len < 1) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = data_len;
	num = 0;

	while (remain) {
		x509_u32 arc_val = 0;
		x509_u32 rbytes = 0;

		if (num > 20) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		ret = _parse_arc(buf, remain, &arc_val, &rbytes);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		num += 1;

		buf += rbytes;
		remain -= rbytes;
	}

	if (num < 1) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*parsed = hdr_len + data_len;

	ret = 0;

out:
	return ret;
}

static int _parse_integer(const x509_u8 *buf, x509_u32 len,
			  tag_class exp_class, x509_u32 exp_type,
			  x509_u32 *hdr_len, x509_u32 *data_len,
			  int pos_or_zero)
{
	int ret;

	if ((buf == NULL) || (len == 0) || (hdr_len == NULL) || (data_len == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*hdr_len = 0;
	*data_len = 0;
	ret = parse_id_len(buf, len, exp_class, exp_type, hdr_len, data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += *hdr_len;

	if (*data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (*data_len > 1) {
		if ((buf[0] == 0) && ((buf[1] & 0x80) == 0)) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		if ((buf[0] == 0xff) && (buf[1] & 0x80)) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
	}

	if (pos_or_zero && (buf[0]) & 0x80) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
	}

	ret = 0;

out:
	return ret;
}

int parse_integer(const x509_u8 *buf, x509_u32 len,
				tag_class exp_class, x509_u32 exp_type,
				x509_u32 *hdr_len, x509_u32 *data_len)
{
	return _parse_integer(buf, len, exp_class, exp_type,
			      hdr_len, data_len, 0);
}

int parse_non_negative_integer(const x509_u8 *buf, x509_u32 len,
					     tag_class exp_class, x509_u32 exp_type,
					     x509_u32 *hdr_len, x509_u32 *data_len)
{
	return _parse_integer(buf, len, exp_class, exp_type,
			      hdr_len, data_len, 1);
}

int parse_boolean(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten)
{
	int ret;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len < 3) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if ((buf[0] != ASN1_TYPE_BOOLEAN) || (buf[1] != 0x01)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	switch (buf[2]) {
	case 0x00: 
	case 0xff: 
		break;
	default:
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
		break;
	}

	*eaten = 3;

	ret = 0;

out:
	return ret;
}

static x509_u8 compute_decimal(x509_u8 d, x509_u8 u)
{
	return (d - 0x30) * 10 + (u - 0x30);
}

static int parse_UTCTime(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten,
			 x509_u16 *year, x509_u8 *month, x509_u8 *day,
			 x509_u8 *hour, x509_u8 *min, x509_u8 *sec)
{
	x509_u16 yyyy;
	x509_u8 mo, dd, hh, mm, ss;
	const x509_u8 c_zero = '0';
	x509_u8 time_type;
	x509_u8 time_len;
	int ret = -X509_FILE_LINE_NUM_ERR;
	x509_u8 i;

	if (buf == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len < 15) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	time_type = buf[0];
	if (time_type != ASN1_TYPE_UTCTime) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	time_len = buf[1];
	if (time_len != 13) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 2;

	for (i = 0; i < 12; i++) {
		if (c_zero > buf[i]) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
		if ((buf[i] - c_zero) > 9) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

	}
	if (buf[12] != 'Z') {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	yyyy = compute_decimal(buf[0], buf[1]);

	if (yyyy >= 50) {
		yyyy += 1900;

	} else {
		yyyy += 2000;

	}

	mo = compute_decimal(buf[ 2], buf[ 3]);
	dd = compute_decimal(buf[ 4], buf[ 5]);
	hh = compute_decimal(buf[ 6], buf[ 7]);
	mm = compute_decimal(buf[ 8], buf[ 9]);
	ss = compute_decimal(buf[10], buf[11]);

	if ((mo > 12) || (dd > 31) || (hh > 23) || (mm > 59) || (ss > 59)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*year  = yyyy;
	*month = mo;
	*day   = dd;
	*hour  = hh;
	*min   = mm;
	*sec   = ss;

	ret = 0;

out:
	if (!ret) {
		*eaten = 15;
	}

	return ret;
}

static x509_u16 compute_year(x509_u8 d1, x509_u8 d2, x509_u8 d3, x509_u8 d4)
{
	return ((x509_u16)d1 - (x509_u16)0x30) * 1000 +
	       ((x509_u16)d2 - (x509_u16)0x30) * 100 +
	       ((x509_u16)d3 - (x509_u16)0x30) * 10 +
	       ((x509_u16)d4 - (x509_u16)0x30);
}

int parse_generalizedTime(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten,
				 x509_u16 *year, x509_u8 *month, x509_u8 *day,
				 x509_u8 *hour, x509_u8 *min, x509_u8 *sec)
{
	x509_u16 yyyy;
	x509_u8 mo, dd, hh, mm, ss;
	const x509_u8 c_zero = '0';
	x509_u8 time_type;
	x509_u8 time_len;
	int ret = -X509_FILE_LINE_NUM_ERR;
	x509_u8 i;

	if (buf == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len < 17) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	time_type = buf[0];
	if (time_type != ASN1_TYPE_GeneralizedTime) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	time_len = buf[1];
	if (time_len != 15) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 2;

	for (i = 0; i < 14; i++) {
		if (c_zero > buf[i]) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
		if ((buf[i] - c_zero) > 9) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
	}
	if (buf[14] != 'Z') {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	yyyy = compute_year(buf[0], buf[1], buf[2], buf[3]);

	mo = compute_decimal(buf[ 4], buf[ 5]);
	dd = compute_decimal(buf[ 6], buf[ 7]);
	hh = compute_decimal(buf[ 8], buf[ 9]);
	mm = compute_decimal(buf[10], buf[11]);
	ss = compute_decimal(buf[12], buf[13]);

	if ((mo > 12) || (dd > 31) || (hh > 23) || (mm > 59) || (ss > 59)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*year  = yyyy;
	*month = mo;
	*day   = dd;
	*hour  = hh;
	*min   = mm;
	*sec   = ss;

	ret = 0;

out:
	if (!ret) {
		*eaten = 17;
	}

	return ret;
}

static int check_utf8_string(const x509_u8 *buf, x509_u32 len)
{
	int ret;
	x509_u8 b0;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	while (len) {
		b0 = buf[0];

		if (b0 <= 0x7f) {                   
			len -= 1;
			buf += 1;
			continue;
		}

		if ((b0 >= 0xc2) && (b0 <= 0xdf)) { 
			if (len < 2) {
				ret = -X509_FILE_LINE_NUM_ERR;
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
			}

			if ((buf[1] & 0xc0) != 0x80) {
				ret = -X509_FILE_LINE_NUM_ERR;
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
			}

			len -= 2;
			buf += 2;
			continue;
		}

		if ((b0 >= 0xe0) && (b0 <= 0xef)) { 
			if (len < 3) {
				ret = -X509_FILE_LINE_NUM_ERR;
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
			}

			if (((buf[1] & 0xc0) != 0x80) ||
			    ((buf[2] & 0xc0) != 0x80)) {
				ret = -X509_FILE_LINE_NUM_ERR;
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
			}

			if ((b0 == 0xe0) &&
			    ((buf[1] < 0xa0) || (buf[1] > 0xbf))) {
				ret = -X509_FILE_LINE_NUM_ERR;
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
			} else if ((b0 == 0xed) &&
				   ((buf[1] < 0x80) || (buf[1] > 0x9f))) {
				ret = -X509_FILE_LINE_NUM_ERR;
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
			}

			len -= 3;
			buf += 3;
			continue;
		}

		if ((b0 >= 0xf0) && (b0 <= 0xf4)) { 
			if (len < 4) {
				ret = -X509_FILE_LINE_NUM_ERR;
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
			}

			if ((b0 == 0xf0) &&
			    ((buf[1] < 0x90) || (buf[1] > 0xbf))) {
				ret = -X509_FILE_LINE_NUM_ERR;
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
			} else if ((b0 == 0xf4) &&
				   ((buf[1] < 0x80) || (buf[1] > 0x8f))) {
				ret = -X509_FILE_LINE_NUM_ERR;
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
			}

			if (((buf[1] & 0xc0) != 0x80) ||
			    ((buf[2] & 0xc0) != 0x80) ||
			    ((buf[3] & 0xc0) != 0x80)) {
				ret = -X509_FILE_LINE_NUM_ERR;
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
			}

			len -= 4;
			buf += 4;
			continue;
		}

		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

static int check_printable_string(const x509_u8 *buf, x509_u32 len)
{
	int ret;
	x509_u32 rbytes;
	x509_u8 c;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	for (rbytes = 0; rbytes < len; rbytes++) {
		c = buf[rbytes];

		if ((c >= 'a' && c <= 'z') ||
		    (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9')) {
			continue;
		}

		switch (c) {
		case 39: 
		case '=':
		case '(':
		case ')':
		case '+':
		case ',':
		case '-':
		case '.':
		case '/':
		case ':':
		case '?':
		case ' ':
			continue;
		default:
			break;
		}

		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

static int check_numeric_string(const x509_u8 *buf, x509_u32 len)
{
	int ret;
	x509_u32 rbytes;
	x509_u8 c;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	for (rbytes = 0; rbytes < len; rbytes++) {
		c = buf[rbytes];

		if ((c < '0') || (c > '9')) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
	}

	ret = 0;

out:
	return ret;
}

static int check_visible_string(const x509_u8 *buf, x509_u32 len)
{
	int ret;
	x509_u32 rbytes = 0;
	x509_u8 c;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	while (rbytes < len) {
		c = buf[rbytes];

		if ((c >= 'a' && c <= 'z') ||
		    (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9')) {
			rbytes += 1;
			continue;
		}

		switch (c) {
		case 39: 
		case '=':
		case '(':
		case ')':
		case '+':
		case ',':
		case '-':
		case '.':
		case '/':
		case ':':
		case '?':
		case ' ':
		case '!':
		case '"':
		case '#':
		case '$':
		case '%':
		case '&':
		case '*':
		case ';':
		case '<':
		case '>':
		case '[':
		case '\\':
		case ']':
		case '^':
		case '_':
		case '`':
		case '{':
		case '|':
		case '}':
		case '~':
			rbytes += 1;
			continue;
		default:
			break;
		}

		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

#ifdef TEMPORARY_LAXIST_DIRECTORY_STRING

static int check_teletex_string(const x509_u8 *buf, x509_u32 len)
{
	return check_visible_string(buf, len);
}

static int check_universal_string(const x509_u8 ATTRIBUTE_UNUSED *buf,
				  x509_u32 ATTRIBUTE_UNUSED len)
{
	return -X509_FILE_LINE_NUM_ERR;
}
#endif

static int check_bmp_string(const x509_u8 ATTRIBUTE_UNUSED *buf,
			    x509_u32 ATTRIBUTE_UNUSED len)
{

	return -X509_FILE_LINE_NUM_ERR;
}

static int check_ia5_string(const x509_u8 *buf, x509_u32 len)
{
	int ret;
	x509_u32 i;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	for (i = 0; i < len; i++) {
		if (buf[i] > 0x7f) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
	}

	ret = 0;

out:
	return ret;
}

int parse_GeneralName(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten, int *empty)
{
	x509_u32 remain = 0, name_len = 0, name_hdr_len = 0, grabbed = 0;
	x509_u8 name_type;
	int ret;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len < 2) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = len;

	name_type = buf[0];
	if (!(name_type & 0x80)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	switch (name_type) {
	case NAME_TYPE_rfc822Name: 
	case NAME_TYPE_dNSName:    
	case NAME_TYPE_URI:        
		buf += 1;
		remain -= 1;

		ret = get_length(buf, remain, &name_len, &grabbed);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
		buf += grabbed;
		remain -= grabbed;

		if (name_len > remain) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		ret = check_ia5_string(buf, name_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		switch (name_type) {
		case NAME_TYPE_rfc822Name: 

			break;
		case NAME_TYPE_dNSName: 

			break;
		case NAME_TYPE_URI: 

			break;
		default:
			break;
		}

		remain -= name_len;
		buf += name_len;
		*eaten = name_len + grabbed + 1;
		*empty = !name_len;

		break;

	case NAME_TYPE_iPAddress: 
		buf += 1;
		remain -= 1;

		ret = get_length(buf, remain, &name_len, &grabbed);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
		buf += grabbed;
		remain -= grabbed;

		if (name_len > remain) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		remain -= name_len;
		buf += name_len;
		*eaten = name_len + grabbed + 1;
		*empty = !name_len;

		break;

	case NAME_TYPE_otherName: 

		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
		break;

	case NAME_TYPE_x400Address: 

		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
		break;

	case NAME_TYPE_directoryName: 
		ret = parse_id_len(buf, remain, CLASS_CONTEXT_SPECIFIC, 4,
				   &name_hdr_len, &name_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += name_hdr_len;
		remain = name_len;

		ret = parse_x509_Name(buf, remain, &grabbed, empty);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += grabbed;
		remain -= grabbed;

		if (remain) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		*eaten = name_hdr_len + name_len;

		break;

	case NAME_TYPE_ediPartyName: 

		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
		break;

	case NAME_TYPE_registeredID: 

		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
		break;

	default:
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
		break;
	}

	ret = 0;

out:
	return ret;
}

int parse_GeneralNames(const x509_u8 *buf, x509_u32 len, tag_class exp_class,
		       x509_u32 exp_type, x509_u32 *eaten)
{
	x509_u32 remain, parsed = 0, hdr_len = 0, data_len = 0;
	int ret, unused = 0;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, exp_class, exp_type,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	remain = data_len;

	while (remain) {
		ret = parse_GeneralName(buf, remain, &parsed, &unused);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		remain -= parsed;
		buf += parsed;
	}

	*eaten = hdr_len + data_len;

	ret = 0;

out:
	return ret;
}

#define MAX_SERIAL_NUM_LEN 22 

int parse_SerialNumber(const x509_u8 *cert, x509_u32 off, x509_u32 len,
		       tag_class exp_class, x509_u32 exp_type,
		       x509_u32 *eaten)
{
	const x509_u8 *buf = cert + off;
	x509_u32 parsed = 0, hdr_len = 0, data_len = 0;
	int ret;

	if ((cert == NULL) || (len == 0) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_integer(buf, len, exp_class, exp_type,
			    &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	parsed = hdr_len + data_len;

	*eaten = parsed;

	if ((data_len == 1) && (buf[2] == 0)) {
#ifndef TEMPORARY_LAXIST_SERIAL_NULL
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
#else
		ret = 0;
		goto out;
#endif
	}

	if (parsed > MAX_SERIAL_NUM_LEN) {
#ifndef TEMPORARY_LAXIST_SERIAL_LENGTH
		ret = -X509_FILE_LINE_NUM_ERR;
	       ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
	       goto out;
#else
	       ret = 0;
#endif
	}

	if (buf[2] & 0x80) {
#ifndef TEMPORARY_LAXIST_SERIAL_NEGATIVE

		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
#else

		ret = 0;
#endif
	}

	ret = 0;

out:
	return ret;
}

int verify_correct_time_use(x509_u8 time_type, x509_u16 yyyy)
{
	int ret;

	switch (time_type) {
	case ASN1_TYPE_UTCTime:
		ret = (yyyy <= 2049) ? 0 : -X509_FILE_LINE_NUM_ERR;
		break;
	case ASN1_TYPE_GeneralizedTime:
		ret = (yyyy >= 2050) ? 0 : -X509_FILE_LINE_NUM_ERR;
		break;
	default:
		ret = -1;
		break;
	}

	return ret;

}

int parse_Time(const x509_u8 *buf, x509_u32 len, x509_u8 *t_type, x509_u32 *eaten,
	       x509_u16 *year, x509_u8 *month, x509_u8 *day,
	       x509_u8 *hour, x509_u8 *min, x509_u8 *sec)
{
	x509_u8 time_type;
	int ret = -X509_FILE_LINE_NUM_ERR;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len < 2) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	time_type = buf[0];

	switch (time_type) {
	case ASN1_TYPE_UTCTime:
		ret = parse_UTCTime(buf, len, eaten, year, month,
				    day, hour, min, sec);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		}
		break;
	case ASN1_TYPE_GeneralizedTime:
		ret = parse_generalizedTime(buf, len, eaten, year, month,
					    day, hour, min, sec);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		}
		break;
	default:
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		break;
	}

	*t_type = time_type;

out:
	if (ret) {
		*eaten = 0;
	}
	return ret;
}

int parse_AKICertSerialNumber(const x509_u8 *cert, x509_u32 off, x509_u32 len,
			      tag_class exp_class, x509_u32 exp_type,
			      x509_u32 *eaten)
{
	int ret;

	ret = parse_SerialNumber(cert, off, len, exp_class, exp_type, eaten);
	if (ret) {
	       ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
	       goto out;
	}

out:
	return ret;
}

int parse_nine_bit_named_bit_list(const x509_u8 *buf, x509_u32 len, x509_u16 *val)
{
	x509_u8 k, non_signif;
	x509_u16 tmp;
	int ret;

	if ((buf == NULL) || (len == 0) || (val == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (buf[0] & 0xf8) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	switch (len) {
	case 1: 
		if (buf[0] != 0) {

			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		} else {

			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		break;

	case 2: 

		if (buf[1] == 0) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		non_signif = 0;

		for (k = 0; k < 8; k++) {
			if ((buf[1] >> k) & 0x1) {
				non_signif = k;
				break;
			}
		}

		if (buf[0] != non_signif) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		tmp = 0;

		for (k = 0; k < 8; k++) {
			const x509_u8 mask[8] = {1, 2, 4, 8, 16, 32, 64, 128 };
			tmp |= (buf[1] & mask[k]) ? mask[7-k] : 0;
		}
		*val = tmp;

		break;

	case 3: 

		if ((buf[0] != 7) || (buf[2] != 0x80)) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		tmp = 0;

		for (k = 0; k < 8; k++) {
			const x509_u8 mask[8] = {1, 2, 4, 8, 16, 32, 64, 128 };
			tmp |= (buf[1] & mask[k]) ? mask[7-k] : 0;
		}
		tmp |= (buf[2] & 0x80) ? 0x0100 : 0x0000;
		*val = tmp;
		break;

	default: 
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

int parse_crldp_reasons(const x509_u8 *buf, x509_u32 len, x509_u32 exp_type, x509_u32 *eaten)
{
	x509_u32 hdr_len = 0, data_len = 0;
	x509_u16 val = 0;
	int ret;

	if ((buf == NULL) || (len == 0) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_CONTEXT_SPECIFIC, exp_type,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	len -= hdr_len;

	ret = parse_nine_bit_named_bit_list(buf, data_len, &val);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = hdr_len + data_len;

	ret = 0;

out:
	return ret;
}

int parse_DistributionPoint(const x509_u8 *buf, x509_u32 len,
			    int *crldp_has_all_reasons, x509_u32 *eaten)
{
	x509_u32 hdr_len = 0, data_len = 0, remain = 0, total_len = 0;
	int dp_or_issuer_present = 0;
	x509_u32 parsed = 0;
	int ret, has_all_reasons = 0;

	if ((buf == NULL) || (len == 0) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len,
			   CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ret = -X509_FILE_LINE_NUM_ERR;
		goto out;
	}

	total_len = hdr_len + data_len;

	remain = data_len;
	buf += hdr_len;

	ret = parse_id_len(buf, remain, CLASS_CONTEXT_SPECIFIC, 0,
			   &hdr_len, &data_len);
	if (!ret) {
		x509_u32 dpn_remain = 0, dpn_eaten = 0;
		x509_u8 dpn_type;

		buf += hdr_len;
		remain -= hdr_len;
		dpn_remain = data_len;

		if (data_len == 0) {
			ret = -X509_FILE_LINE_NUM_ERR;
			goto out;
		}

		dpn_type = buf[0];

		switch (dpn_type) {
		case 0xa0: 
			ret = parse_GeneralNames(buf, dpn_remain,
						 CLASS_CONTEXT_SPECIFIC, 0,
						 &dpn_eaten);
			if (ret) {
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
			}

			dpn_remain -= dpn_eaten;
			buf += dpn_eaten;
			break;

		case 0xa1: 

			ret = -X509_FILE_LINE_NUM_ERR;
			goto out;
			break;

		default:
			ret = -X509_FILE_LINE_NUM_ERR;
			goto out;
			break;
		}

		if (dpn_remain) {
			ret = -X509_FILE_LINE_NUM_ERR;
			goto out;
		}

		dp_or_issuer_present |= 1;

		remain -= data_len;
	}

	ret = parse_crldp_reasons(buf, remain, 0x01, &parsed);
	if (!ret) {
		buf += parsed;
		remain -= parsed;
	} else {

		has_all_reasons = 1;
	}

	ret = parse_GeneralNames(buf, remain, CLASS_CONTEXT_SPECIFIC, 2,
				 &parsed);
	if (!ret) {

		dp_or_issuer_present |= 1;

		buf += parsed;
		remain -= parsed;
	}

	if (remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (!dp_or_issuer_present) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = total_len;
	*crldp_has_all_reasons = has_all_reasons;

	ret = 0;

out:
	return ret;
}

static int parse_AccessDescription(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten)
{
	const x509_u8 id_ad_caIssuers_oid[] = { 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05,
					   0x05, 0x07, 0x30, 0x01 };
	const x509_u8 id_ad_ocsp_oid[] = { 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05,
				      0x05, 0x07, 0x30, 0x02 };
	x509_u32 remain, hdr_len = 0, data_len = 0, oid_len = 0;
	x509_u32 al_len = 0, saved_ad_len = 0;
	int ret, found, unused;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = len;

	ret = parse_id_len(buf, remain,
			   CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	saved_ad_len = hdr_len + data_len;

	remain -= hdr_len;

	buf += hdr_len;

	ret = parse_OID(buf, data_len, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	found = 0;

	if (oid_len == sizeof(id_ad_caIssuers_oid)) {
		found = !bufs_differ(buf, id_ad_caIssuers_oid, oid_len);
	}

	if ((!found) && (oid_len == sizeof(id_ad_ocsp_oid))) {
		found = !bufs_differ(buf, id_ad_ocsp_oid, oid_len);
	}

	if (!found) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += oid_len;
	remain -= oid_len;
	data_len -= oid_len;

	ret = parse_GeneralName(buf, data_len, &al_len, &unused);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += al_len;

	remain -= al_len;
	data_len -= al_len;

	if (data_len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = saved_ad_len;

	ret = 0;

out:
	return ret;
}

int parse_AIA(const x509_u8 *cert, x509_u32 off, x509_u32 len, int critical)
{
	x509_u32 hdr_len = 0, data_len = 0, remain;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((cert == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (critical) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = len;

	ret = parse_id_len(buf, remain, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	remain -= hdr_len;

	if (remain != data_len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (!remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	while (remain) {
		x509_u32 parsed = 0;

		ret = parse_AccessDescription(buf, remain, &parsed);
		if (ret) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		remain -= parsed;
		buf += parsed;
	}

	ret = 0;

out:
	return ret;
}

const _sig_alg * find_sig_alg_by_oid(const x509_u8 *buf, x509_u32 len)
{
	const _sig_alg *found = NULL;
	const _sig_alg *cur = NULL;
	x509_u8 k;

	if ((buf == NULL) || (len == 0)) {
		goto out;
	}

	for (k = 0; k < num_known_sig_algs; k++) {
		int ret;

		cur = known_sig_algs[k];

		if (cur->alg_der_oid_len != len) {
			continue;
		}

		ret = !bufs_differ(cur->alg_der_oid, buf, cur->alg_der_oid_len);
		if (ret) {
			found = cur;
			break;
		}
	}

out:
	return found;
}

const _hash_alg * find_hash_by_oid(const x509_u8 *buf, x509_u32 len)
{
	const _hash_alg *found = NULL;
	const _hash_alg *cur = NULL;
	x509_u8 k;

	if ((buf == NULL) || (len == 0)) {
		goto out;
	}

	for (k = 0; k < NUM_KNOWN_HASHES; k++) {
		int ret;

		cur = known_hashes[k];

		if (cur->alg_der_oid_len != len) {
			continue;
		}

		ret = !bufs_differ(cur->alg_der_oid, buf, cur->alg_der_oid_len);
		if (ret) {
			found = cur;
			break;
		}
	}

out:
	return found;
}

const _curve * find_curve_by_oid(const x509_u8 *buf, x509_u32 len)
{
	const _curve *found = NULL;
	const _curve *cur = NULL;
	x509_u8 k;

	if ((buf == NULL) || (len == 0)) {
		goto out;
	}

	for (k = 0; k < NUM_KNOWN_CURVES; k++) {
		int ret;

		cur = known_curves[k];

		if (cur->crv_der_oid_len != len) {
			continue;
		}

		ret = !bufs_differ(cur->crv_der_oid, buf, cur->crv_der_oid_len);
		if (ret) {
			found = cur;
			break;
		}
	}

out:
	return found;
}

static const x509_u8 _dn_oid_cn[] =        { 0x06, 0x03, 0x55, 0x04, 0x03 };
static const x509_u8 _dn_oid_surname[] =   { 0x06, 0x03, 0x55, 0x04, 0x04 };
static const x509_u8 _dn_oid_serial[] =    { 0x06, 0x03, 0x55, 0x04, 0x05 };
static const x509_u8 _dn_oid_country[] =   { 0x06, 0x03, 0x55, 0x04, 0x06 };
static const x509_u8 _dn_oid_locality[] =  { 0x06, 0x03, 0x55, 0x04, 0x07 };
static const x509_u8 _dn_oid_state[] =     { 0x06, 0x03, 0x55, 0x04, 0x08 };
static const x509_u8 _dn_oid_org[] =       { 0x06, 0x03, 0x55, 0x04, 0x0a };
static const x509_u8 _dn_oid_org_unit[] =  { 0x06, 0x03, 0x55, 0x04, 0x0b };
static const x509_u8 _dn_oid_title[] =     { 0x06, 0x03, 0x55, 0x04, 0x0c };
static const x509_u8 _dn_oid_name[] =      { 0x06, 0x03, 0x55, 0x04, 0x29 };
static const x509_u8 _dn_oid_emailaddress[] = { 0x06, 0x09, 0x2a, 0x86, 0x48,
					   0x86, 0xf7, 0x0d, 0x01, 0x09,
					   0x01  };
static const x509_u8 _dn_oid_given_name[] = { 0x06, 0x03, 0x55, 0x04, 0x2a };
static const x509_u8 _dn_oid_initials[] =  { 0x06, 0x03, 0x55, 0x04, 0x2b };
static const x509_u8 _dn_oid_gen_qual[] =  { 0x06, 0x03, 0x55, 0x04, 0x2c };
static const x509_u8 _dn_oid_dn_qual[] =   { 0x06, 0x03, 0x55, 0x04, 0x2e };
static const x509_u8 _dn_oid_pseudo[] =    { 0x06, 0x03, 0x55, 0x04, 0x41 };
static const x509_u8 _dn_oid_dc[] =        { 0x06, 0x0a, 0x09, 0x92, 0x26,
					0x89, 0x93, 0xf2, 0x2c, 0x64,
					0x01, 0x19 };
static const x509_u8 _dn_oid_ogrn[] =      { 0x06, 0x05, 0x2a, 0x85, 0x03,
					0x64, 0x01 };
static const x509_u8 _dn_oid_snils[] =     { 0x06, 0x05, 0x2a, 0x85, 0x03,
					0x64, 0x03 };
static const x509_u8 _dn_oid_ogrnip[] =    { 0x06, 0x05, 0x2a, 0x85, 0x03,
					0x64, 0x05 };
static const x509_u8 _dn_oid_inn[] =       { 0x06, 0x08, 0x2a, 0x85, 0x03,
					0x03, 0x81, 0x03, 0x01, 0x01 };
static const x509_u8 _dn_oid_street_address[] = { 0x06, 0x03, 0x55, 0x04, 0x09 };

#define STR_TYPE_UTF8_STRING      12
#define STR_TYPE_NUMERIC_STRING   18
#define STR_TYPE_PRINTABLE_STRING 19
#define STR_TYPE_TELETEX_STRING   20
#define STR_TYPE_IA5_STRING       22
#define STR_TYPE_VISIBLE_STRING   26
#define STR_TYPE_UNIVERSAL_STRING 28
#define STR_TYPE_BMP_STRING       30

static int parse_directory_string(const x509_u8 *buf, x509_u32 len, x509_u32 lb, x509_u32 ub)
{
	int ret = -X509_FILE_LINE_NUM_ERR;
	x509_u8 str_type;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len < 2) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	str_type = buf[0];

	len -= 2;
	if (buf[1] != len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 2;

	if ((len < lb) || (len > ub)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	switch (str_type) {
	case STR_TYPE_PRINTABLE_STRING:
		ret = check_printable_string(buf, len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		}
		break;
	case STR_TYPE_UTF8_STRING:
		ret = check_utf8_string(buf, len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		}
		break;
#ifdef TEMPORARY_LAXIST_DIRECTORY_STRING

	case STR_TYPE_TELETEX_STRING:
		ret = check_teletex_string(buf, len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		}
		break;
	case STR_TYPE_UNIVERSAL_STRING:
		ret = check_universal_string(buf, len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		}
		break;
	case STR_TYPE_BMP_STRING:
		ret = check_bmp_string(buf, len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		}
		break;
	case STR_TYPE_IA5_STRING:
		ret = check_ia5_string(buf, len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		}
		break;
	case STR_TYPE_NUMERIC_STRING:
		ret = check_numeric_string(buf, len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		}
		break;
#endif
	default:
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		break;
	}

	if (ret) {
		goto out;
	}

	ret = 0;

out:
	return ret;
}

static int parse_printable_string(const x509_u8 *buf, x509_u32 len, x509_u32 lb, x509_u32 ub)
{
	int ret = -X509_FILE_LINE_NUM_ERR;
	x509_u8 str_type;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len < 2) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	str_type = buf[0];
	if (str_type != STR_TYPE_PRINTABLE_STRING) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	len -= 2;
	if (buf[1] != len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 2;

	if ((len < lb) || (len > ub)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = check_printable_string(buf, len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

static int parse_numeric_string(const x509_u8 *buf, x509_u32 len, x509_u32 lb, x509_u32 ub)
{
	int ret = -X509_FILE_LINE_NUM_ERR;
	x509_u8 str_type;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len < 2) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	str_type = buf[0];
	if (str_type != STR_TYPE_NUMERIC_STRING) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	len -= 2;
	if (buf[1] != len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 2;

	if ((len < lb) || (len > ub)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = check_numeric_string(buf, len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

int parse_ia5_string(const x509_u8 *buf, x509_u32 len, x509_u32 lb, x509_u32 ub)
{
	int ret = -X509_FILE_LINE_NUM_ERR;
	x509_u8 str_type;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len < 2) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	str_type = buf[0];
	if (str_type != STR_TYPE_IA5_STRING) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	len -= 2;
	if (buf[1] != len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 2;

	if ((len < lb) || (len > ub)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = check_ia5_string(buf, len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

#ifdef TEMPORARY_LAXIST_EMAILADDRESS_WITH_UTF8_ENCODING

static int parse_utf8_string(const x509_u8 *buf, x509_u32 len, x509_u32 lb, x509_u32 ub)
{
	int ret = -X509_FILE_LINE_NUM_ERR;
	x509_u8 str_type;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len < 2) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	str_type = buf[0];
	if (str_type != STR_TYPE_IA5_STRING) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	len -= 2;
	if (buf[1] != len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 2;

	if ((len < lb) || (len > ub)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = check_utf8_string(buf, len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}
#endif

#ifdef TEMPORARY_LAXIST_RDN_UPPER_BOUND
#define UB_COMMON_NAME 192
#else
#define UB_COMMON_NAME 64
#endif

static int parse_rdn_val_cn(const x509_u8 *buf, x509_u32 len)
{
	return parse_directory_string(buf, len, 1, UB_COMMON_NAME);
}

#define UB_NAME 32768

static int parse_rdn_val_x520name(const x509_u8 *buf, x509_u32 len)
{
	return parse_directory_string(buf, len, 1, UB_NAME);
}

#define UB_EMAILADDRESS 255

static int parse_rdn_val_emailaddress(const x509_u8 *buf, x509_u32 len)
{
	int ret;

	ret = parse_ia5_string(buf, len, 1, UB_EMAILADDRESS);

#ifdef TEMPORARY_LAXIST_EMAILADDRESS_WITH_UTF8_ENCODING
	if (ret) {
		ret = parse_utf8_string(buf, len, 1, UB_EMAILADDRESS);
	}
#endif

	return ret;
}

#define UB_SERIAL_NUMBER 64

static int parse_rdn_val_serial(const x509_u8 *buf, x509_u32 len)
{
	int ret;

	ret = parse_printable_string(buf, len, 1, UB_SERIAL_NUMBER);
	if (ret) {
#ifdef TEMPORARY_LAXIST_SERIAL_RDN_AS_IA5STRING
		ret = parse_ia5_string(buf, len, 1, UB_SERIAL_NUMBER);
#endif
	}

	return ret;
}

#define UB_COUNTRY 2

static int parse_rdn_val_country(const x509_u8 *buf, x509_u32 len)
{
	return parse_directory_string(buf, len, UB_COUNTRY, UB_COUNTRY);
}

#define UB_LOCALITY_NAME 128

static int parse_rdn_val_locality(const x509_u8 *buf, x509_u32 len)
{
	return parse_directory_string(buf, len, 1, UB_LOCALITY_NAME);
}

#define UB_STATE_NAME 128

static int parse_rdn_val_state(const x509_u8 *buf, x509_u32 len)
{
	return parse_directory_string(buf, len, 1, UB_STATE_NAME);
}

#ifdef TEMPORARY_LAXIST_RDN_UPPER_BOUND
#define UB_ORGANIZATION_NAME 64
#else
#define UB_ORGANIZATION_NAME 128
#endif

static int parse_rdn_val_org(const x509_u8 *buf, x509_u32 len)
{
	return parse_directory_string(buf, len, 1, UB_ORGANIZATION_NAME);
}

#ifdef TEMPORARY_LAXIST_RDN_UPPER_BOUND
#define UB_ORGANIZATION_UNIT_NAME 128
#else
#define UB_ORGANIZATION_UNIT_NAME 64
#endif

static int parse_rdn_val_org_unit(const x509_u8 *buf, x509_u32 len)
{
	return parse_directory_string(buf, len, 1, UB_ORGANIZATION_UNIT_NAME);
}

#define UB_TITLE_NAME 64

static int parse_rdn_val_title(const x509_u8 *buf, x509_u32 len)
{
	return parse_directory_string(buf, len, 1, UB_TITLE_NAME);
}

static int parse_rdn_val_dn_qual(const x509_u8 *buf, x509_u32 len)
{

	return parse_printable_string(buf, len, 1, ASN1_MAX_BUFFER_SIZE);
}

static inline int _is_digit(x509_u8 c)
{
	return ((c >= '0') && (c <= '9'));
}

static inline int _is_alpha(x509_u8 c)
{
	return (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')));
}

#define UB_DC 63

static int parse_rdn_val_dc(const x509_u8 *buf, x509_u32 len)
{
	int ret;
	x509_u32 i;
	x509_u8 c;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_ia5_string(buf, len, 1, UB_DC);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += 2;
	len -= 2;

	c = buf[0];
	ret = _is_alpha(c) || _is_digit(c);
	if (!ret) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += 1;
	len -= 1;

	if (!len) { 
		ret = 0;
		goto out;
	}

	c = buf[len - 1];
	ret = _is_alpha(c) || _is_digit(c);
	if (!ret) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += 1;
	len -= 1;

	for (i = 0; i < len; i++) {
		c = buf[i];
		ret = _is_digit(c) || _is_alpha(c) || (c == '-');
		if (!ret) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
	}

	ret = 0;

out:
	return ret;
}

#define UB_PSEUDONYM 128

static int parse_rdn_val_pseudo(const x509_u8 *buf, x509_u32 len)
{
	return parse_directory_string(buf, len, 1, UB_PSEUDONYM);
}

#define UB_OGRN 13

static int parse_rdn_val_ogrn(const x509_u8 *buf, x509_u32 len)
{
	return parse_numeric_string(buf, len, 1, UB_OGRN);
}

#define UB_SNILS 11

static int parse_rdn_val_snils(const x509_u8 *buf, x509_u32 len)
{
	return parse_numeric_string(buf, len, 1, UB_SNILS);
}

#define UB_OGRNIP 15

static int parse_rdn_val_ogrnip(const x509_u8 *buf, x509_u32 len)
{
	return parse_numeric_string(buf, len, 1, UB_OGRNIP);
}

#define UB_INN 12

static int parse_rdn_val_inn(const x509_u8 *buf, x509_u32 len)
{
	return parse_numeric_string(buf, len, 1, UB_INN);
}

#define UB_STREET_ADDRESS 64 

static int parse_rdn_val_street_address(const x509_u8 *buf, x509_u32 len)
{
	return parse_directory_string(buf, len, 1, UB_STREET_ADDRESS);
}

typedef struct {
	const x509_u8 *oid;
	x509_u8 oid_len;
	int (*parse_rdn_val)(const x509_u8 *buf, x509_u32 len);
} _name_oid;

static int parse_rdn_val_bad_oid(const x509_u8 *buf, x509_u32 len)
{
	(void) buf;
	(void) len;
	return 0;
}

static const _name_oid generic_unsupported_rdn_oid = {
	.oid = NULL,
	.oid_len = 0,
	.parse_rdn_val = parse_rdn_val_bad_oid
};

static const _name_oid known_dn_oids[] = {
	{ .oid = _dn_oid_cn,
	  .oid_len = sizeof(_dn_oid_cn),
	  .parse_rdn_val = parse_rdn_val_cn
	},
	{ .oid = _dn_oid_surname,
	  .oid_len = sizeof(_dn_oid_surname),
	  .parse_rdn_val = parse_rdn_val_x520name
	},
	{ .oid = _dn_oid_serial,
	  .oid_len = sizeof(_dn_oid_serial),
	  .parse_rdn_val = parse_rdn_val_serial
	},
	{ .oid = _dn_oid_country,
	  .oid_len = sizeof(_dn_oid_country),
	  .parse_rdn_val = parse_rdn_val_country
	},
	{ .oid = _dn_oid_locality,
	  .oid_len = sizeof(_dn_oid_locality),
	  .parse_rdn_val = parse_rdn_val_locality
	},
	{ .oid = _dn_oid_state,
	  .oid_len = sizeof(_dn_oid_state),
	  .parse_rdn_val = parse_rdn_val_state
	},
	{ .oid = _dn_oid_org,
	  .oid_len = sizeof(_dn_oid_org),
	  .parse_rdn_val = parse_rdn_val_org
	},
	{ .oid = _dn_oid_org_unit,
	  .oid_len = sizeof(_dn_oid_org_unit),
	  .parse_rdn_val = parse_rdn_val_org_unit
	},
	{ .oid = _dn_oid_title,
	  .oid_len = sizeof(_dn_oid_title),
	  .parse_rdn_val = parse_rdn_val_title
	},
	{ .oid = _dn_oid_name,
	  .oid_len = sizeof(_dn_oid_name),
	  .parse_rdn_val = parse_rdn_val_x520name
	},
	{ .oid = _dn_oid_emailaddress,
	  .oid_len = sizeof(_dn_oid_emailaddress),
	  .parse_rdn_val = parse_rdn_val_emailaddress
	},
	{ .oid = _dn_oid_given_name,
	  .oid_len = sizeof(_dn_oid_given_name),
	  .parse_rdn_val = parse_rdn_val_x520name
	},
	{ .oid = _dn_oid_initials,
	  .oid_len = sizeof(_dn_oid_initials),
	  .parse_rdn_val = parse_rdn_val_x520name
	},
	{ .oid = _dn_oid_gen_qual,
	  .oid_len = sizeof(_dn_oid_gen_qual),
	  .parse_rdn_val = parse_rdn_val_x520name
	},
	{ .oid = _dn_oid_dn_qual,
	  .oid_len = sizeof(_dn_oid_dn_qual),
	  .parse_rdn_val = parse_rdn_val_dn_qual
	},
	{ .oid = _dn_oid_pseudo,
	  .oid_len = sizeof(_dn_oid_pseudo),
	  .parse_rdn_val = parse_rdn_val_pseudo
	},
	{ .oid = _dn_oid_dc,
	  .oid_len = sizeof(_dn_oid_dc),
	  .parse_rdn_val = parse_rdn_val_dc
	},
	{ .oid = _dn_oid_ogrn,
	  .oid_len = sizeof(_dn_oid_ogrn),
	  .parse_rdn_val = parse_rdn_val_ogrn
	},
	{ .oid = _dn_oid_snils,
	  .oid_len = sizeof(_dn_oid_snils),
	  .parse_rdn_val = parse_rdn_val_snils
	},
	{ .oid = _dn_oid_ogrnip,
	  .oid_len = sizeof(_dn_oid_ogrnip),
	  .parse_rdn_val = parse_rdn_val_ogrnip
	},
	{ .oid = _dn_oid_inn,
	  .oid_len = sizeof(_dn_oid_inn),
	  .parse_rdn_val = parse_rdn_val_inn
	},
	{ .oid = _dn_oid_street_address,
	  .oid_len = sizeof(_dn_oid_street_address),
	  .parse_rdn_val = parse_rdn_val_street_address
	},
};

#define NUM_KNOWN_DN_OIDS (sizeof(known_dn_oids) / sizeof(known_dn_oids[0]))

static const _name_oid * find_dn_by_oid(const x509_u8 *buf, x509_u32 len)
{
	const _name_oid *found = NULL;
	const _name_oid *cur = NULL;
	x509_u8 k;

	if ((buf == NULL) || (len == 0)) {
		goto out;
	}

	for (k = 0; k < NUM_KNOWN_DN_OIDS; k++) {
		int ret;

		cur = &known_dn_oids[k];

		if (cur->oid_len != len) {
			continue;
		}

		ret = !bufs_differ(cur->oid, buf, cur->oid_len);
		if (ret) {
			found = cur;

			break;
		}
	}

out:

	return found;
}

static int parse_AttributeTypeAndValue(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 hdr_len = 0;
	x509_u32 data_len = 0;
	x509_u32 oid_len = 0;
	x509_u32 parsed;
	const _name_oid *cur = NULL;
	int ret;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	parsed = hdr_len + data_len;

	buf += hdr_len;
	len -= hdr_len;

	ret = parse_OID(buf, data_len, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	cur = find_dn_by_oid(buf, oid_len);
	if (cur == NULL) {
#ifndef TEMPORARY_LAXIST_HANDLE_ALL_REMAINING_RDN_OIDS

		(void)generic_unsupported_rdn_oid;
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
#else
		cur = &generic_unsupported_rdn_oid;
#endif
	}

	data_len -= oid_len;
	buf += oid_len;

	ret = cur->parse_rdn_val(buf, data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = parsed;

	ret = 0;

out:
	return ret;
}

static int parse_RelativeDistinguishedName(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 hdr_len = 0;
	x509_u32 data_len = 0;
	x509_u32 rdn_remain, saved_rdn_len;
	int ret;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SET,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	saved_rdn_len = hdr_len + data_len;
	buf += hdr_len;
	rdn_remain = data_len;

	while (rdn_remain) {
		x509_u32 parsed = 0;

		ret = parse_AttributeTypeAndValue(buf, rdn_remain, &parsed);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		rdn_remain -= parsed;
		buf += parsed;
	}

	*eaten = saved_rdn_len;

	ret = 0;

out:
	return ret;
}

int parse_x509_Name(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten, int *empty)
{
	x509_u32 name_hdr_len = 0;
	x509_u32 name_data_len = 0;
	x509_u32 remain = 0;
	int ret;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &name_hdr_len, &name_data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += name_hdr_len;
	remain = name_data_len;

	while (remain) {
		x509_u32 parsed = 0;

		ret = parse_RelativeDistinguishedName(buf, remain, &parsed);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += parsed;
		remain -= parsed;
	}

	*eaten = name_hdr_len + name_data_len;
	*empty = !name_data_len;

	ret = 0;

out:
	return ret;
}

int parse_DisplayText(const x509_u8 *buf, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 hdr_len = 0, data_len = 0;
	x509_u8 str_type;
	int ret = -1;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	str_type = buf[0];

	switch (str_type) {
	case STR_TYPE_UTF8_STRING:    
	case STR_TYPE_IA5_STRING:     
	case STR_TYPE_VISIBLE_STRING: 
	case STR_TYPE_BMP_STRING:     
		ret = parse_id_len(buf, len, CLASS_UNIVERSAL, str_type,
				   &hdr_len, &data_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += hdr_len;

		switch (str_type) {
		case STR_TYPE_UTF8_STRING:
			ret = check_utf8_string(buf, data_len);
			if (ret) {
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
			}
			break;
		case STR_TYPE_IA5_STRING:
			ret = check_ia5_string(buf, data_len);
			if (ret) {
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
			}
			break;
		case STR_TYPE_VISIBLE_STRING:
			ret = check_visible_string(buf, data_len);
			if (ret) {
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
			}
			break;
		case STR_TYPE_BMP_STRING:
			ret = check_bmp_string(buf, data_len);
			if (ret) {
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
			}
			break;
		default:
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
			break;
		}

		*eaten = hdr_len + data_len;

		break;
	default:
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
		break;
	}

out:
	return ret;
}

int parse_sig_monkey(sig_params *params,
			    const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 remain, hdr_len = 0, data_len = 0;
	const x509_u8 *buf = cert  + off;
	int ret;

	if ((params == NULL) || (cert == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_BIT_STRING,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;

	if (buf[0] != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 1;
	off += 1;
	remain = data_len - 1;

	params->monkeysphere.sig_raw_off = off;
	params->monkeysphere.sig_raw_len = remain;

	*eaten = hdr_len + data_len;

	ret = 0;

out:
	return ret;
}

static int sig_gost_extract_r_s(const x509_u8 *buf, x509_u32 len,
				x509_u32 *r_start_off, x509_u32 *r_len,
				x509_u32 *s_start_off, x509_u32 *s_len,
				x509_u32 *eaten)
{
	x509_u32 remain, hdr_len = 0, data_len = 0, off = 0;
	int ret;

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_BIT_STRING,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;

	if (buf[0] != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 1;
	off += 1;
	remain = data_len - 1;

	*s_start_off = off;
	*s_len = remain / 2;
	*r_start_off = off + *s_len;
	*r_len = *s_len;
	*eaten = hdr_len + data_len;

	ret = 0;

out:
	return ret;
}

int parse_sig_gost94(sig_params *params,
		     const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 r_start_off = 0, r_len = 0, s_start_off = 0, s_len = 0;
	const x509_u8 *buf =  cert + off;
	int ret;

	if ((params == NULL) || (cert == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = sig_gost_extract_r_s(buf, len, &r_start_off, &r_len,
				   &s_start_off, &s_len, eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	params->gost_r3410_94.r_raw_off = off + r_start_off;
	params->gost_r3410_94.r_raw_len = r_len;
	params->gost_r3410_94.s_raw_off = off + s_start_off;
	params->gost_r3410_94.s_raw_len = s_len;
	ret = 0;

out:
	return ret;
}

int parse_sig_gost2001(sig_params *params,
		       const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 r_start_off = 0, r_len = 0, s_start_off = 0, s_len = 0;
	const x509_u8 *buf =  cert + off;
	int ret;

	if ((params == NULL) || (cert == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = sig_gost_extract_r_s(buf, len, &r_start_off, &r_len,
				   &s_start_off, &s_len, eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	params->gost_r3410_2001.r_raw_off = off + r_start_off;
	params->gost_r3410_2001.r_raw_len = r_len;
	params->gost_r3410_2001.s_raw_off = off + s_start_off;
	params->gost_r3410_2001.s_raw_len = s_len;
	ret = 0;

out:
	return ret;
}

int parse_sig_bign(sig_params *params,
		   const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 remain, hdr_len = 0, data_len = 0;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((params == NULL) || (cert == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_BIT_STRING,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;

	if (buf[0] != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 1;
	off += 1;
	remain = data_len - 1;

	params->bign.sig_raw_off = off;
	params->bign.sig_raw_len = remain;
	*eaten = hdr_len + data_len;
	ret = 0;

out:
	return ret;
}

int parse_sig_gost2012_256(sig_params *params,
			   const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 r_start_off = 0, r_len = 0, s_start_off = 0, s_len = 0;
	const x509_u8 *buf =  cert + off;
	int ret;

	if ((params == NULL) || (cert == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = sig_gost_extract_r_s(buf, len, &r_start_off, &r_len,
				   &s_start_off, &s_len, eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	params->gost_r3410_2012_256.r_raw_off = off + r_start_off;
	params->gost_r3410_2012_256.r_raw_len = r_len;
	params->gost_r3410_2012_256.s_raw_off = off + s_start_off;
	params->gost_r3410_2012_256.s_raw_len = s_len;
	ret = 0;

out:
	return ret;
}

int parse_sig_gost2012_512(sig_params *params,
			   const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 r_start_off = 0, r_len = 0, s_start_off = 0, s_len = 0;
	const x509_u8 *buf =  cert + off;
	int ret;

	if ((params == NULL) || (cert == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = sig_gost_extract_r_s(buf, len, &r_start_off, &r_len,
				   &s_start_off, &s_len, eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	params->gost_r3410_2012_512.r_raw_off = off + r_start_off;
	params->gost_r3410_2012_512.r_raw_len = r_len;
	params->gost_r3410_2012_512.s_raw_off = off + s_start_off;
	params->gost_r3410_2012_512.s_raw_len = s_len;
	ret = 0;

out:
	return ret;
}

int parse_sig_rsa_helper(const x509_u8 *buf, x509_u32 len,
			 x509_u32 *bs_data_start_off, x509_u32 *bs_data_len,
			 x509_u32 *eaten)
{
	x509_u32 hdr_len = 0, data_len = 0;
	int ret;

	if ((buf == NULL) || (len == 0) || (eaten == NULL) ||
	    (bs_data_start_off == NULL) || (bs_data_len == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_BIT_STRING,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;

	if (len != (hdr_len + data_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (buf[0] != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 1;
	*bs_data_start_off = hdr_len + 1;
	*bs_data_len = data_len - 1;
	*eaten = hdr_len + data_len;
	ret = 0;

out:
	return ret;
}

int parse_sig_eddsa(const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 exp_sig_len,
		    x509_u32 *r_start_off, x509_u32 *r_len, x509_u32 *s_start_off, x509_u32 *s_len,
		    x509_u32 *eaten)
{
	x509_u32 comp_len, sig_len = 0, hdr_len = 0, data_len = 0, remain = 0;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((cert == NULL) || (len == 0) || (eaten == NULL) ||
	    (r_start_off == NULL) || (r_len == NULL) ||
	    (s_start_off == NULL) || (s_len == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_BIT_STRING,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;

	if (len != (hdr_len + data_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (buf[0] != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 1;
	sig_len = data_len - 1;
	if (sig_len != exp_sig_len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	comp_len = sig_len / 2;

	if (sig_len != (comp_len * 2)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*r_start_off = off + hdr_len + 1;
	*r_len = comp_len;

	*s_start_off = off + hdr_len + 1 + comp_len;
	*s_len = comp_len;

	if (remain != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = hdr_len + data_len;
	ret = 0;

out:
	return ret;
}

#define ED448_SIG_LEN 114

int parse_sig_ed448(sig_params *params,
		    const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten)
{
	int ret;

	if ((params == NULL) || (cert == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_sig_eddsa(cert, off, len, ED448_SIG_LEN,
			      &params->ed448.r_raw_off,
			      &params->ed448.r_raw_len,
			      &params->ed448.s_raw_off,
			      &params->ed448.s_raw_len,
			      eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

#define ED25519_SIG_LEN 64

int parse_sig_ed25519(sig_params *params,
		      const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten)
{
	int ret;

	if ((params == NULL) || (cert == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_sig_eddsa(cert, off, len, ED25519_SIG_LEN,
			      &params->ed25519.r_raw_off,
			      &params->ed25519.r_raw_len,
			      &params->ed25519.s_raw_off,
			      &params->ed25519.s_raw_len,
			      eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

int sig_dsa_based_extract_r_s(const x509_u8 *buf, x509_u32 len,
			      x509_u32 *r_start_off, x509_u32 *r_len,
			      x509_u32 *s_start_off, x509_u32 *s_len,
			      x509_u32 *eaten)
{
	x509_u32 bs_hdr_len = 0, bs_data_len = 0, sig_len = 0, hdr_len = 0;
	x509_u32 data_len = 0, remain = 0, saved_sig_len = 0;
	x509_u32 integer_len = 0;
	x509_u32 off;
	int ret;

	if ((buf == NULL) || (len == 0) || (eaten == NULL) ||
	    (r_start_off == NULL) || (r_len == NULL) ||
	    (s_start_off == NULL) || (s_len == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_BIT_STRING,
			   &bs_hdr_len, &bs_data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	saved_sig_len = bs_hdr_len + bs_data_len;

	buf += bs_hdr_len;
	off = bs_hdr_len;

	if (bs_data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (buf[0] != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	buf += 1;
	off += 1;
	sig_len = bs_data_len - 1;

	ret = parse_id_len(buf, sig_len,
			   CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (sig_len != (hdr_len + data_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = data_len;
	buf += hdr_len;
	off += hdr_len;

	ret = parse_non_negative_integer(buf, remain, CLASS_UNIVERSAL,
					 ASN1_TYPE_INTEGER,
					 &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	integer_len = hdr_len + data_len;

	remain -= integer_len;
	buf += integer_len;
	*r_start_off = off + hdr_len;

	*r_len = data_len;

	off += hdr_len + data_len;

	ret = parse_non_negative_integer(buf, remain, CLASS_UNIVERSAL,
					 ASN1_TYPE_INTEGER,
					 &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	integer_len = hdr_len + data_len;

	remain -= integer_len;
	buf += hdr_len + data_len;
	*s_start_off = off + hdr_len;

	*s_len = data_len;

	off += integer_len;

	if (remain != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = saved_sig_len;

	ret = 0;

out:
	return ret;
}

int parse_sig_ecdsa(sig_params *params,
		    const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 r_start_off = 0, r_len= 0, s_start_off = 0, s_len = 0;
	const x509_u8 *buf =  cert + off;
	int ret;

	if ((params == NULL) || (cert == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = sig_dsa_based_extract_r_s(buf, len, &r_start_off, &r_len,
					&s_start_off, &s_len, eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	params->ecdsa.r_raw_off = off + r_start_off;
	params->ecdsa.r_raw_len = r_len;
	params->ecdsa.s_raw_off = off + s_start_off;
	params->ecdsa.s_raw_len = s_len;
	ret = 0;

out:
	return ret;
}

int parse_sig_sm2(sig_params *params,
		  const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 r_start_off = 0, r_len = 0, s_start_off = 0, s_len = 0;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((params == NULL) || (cert == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = sig_dsa_based_extract_r_s(buf, len, &r_start_off, &r_len,
					&s_start_off, &s_len, eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	params->sm2.r_raw_off = off + r_start_off;
	params->sm2.r_raw_len = r_len;
	params->sm2.s_raw_off = off + s_start_off;
	params->sm2.s_raw_len = s_len;
	ret = 0;

out:
	return ret;
}

int parse_sig_rsa_pkcs1_v15(sig_params *params,
			    const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 bs_data_start_off = 0, bs_data_len = 0;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((params == NULL) || (cert == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_sig_rsa_helper(buf, len,
				   &bs_data_start_off, &bs_data_len,
				   eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	params->rsa_pkcs1_v1_5.sig_raw_off = off + bs_data_start_off;
	params->rsa_pkcs1_v1_5.sig_raw_len = bs_data_len;

out:
	return ret;
}

int parse_sig_rsa_ssa_pss(sig_params *params,
			  const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 bs_data_start_off = 0, bs_data_len = 0;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((params == NULL) || (cert == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_sig_rsa_helper(buf, len,
				   &bs_data_start_off, &bs_data_len,
				   eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	params->rsa_ssa_pss.sig_raw_off = off + bs_data_start_off;
	params->rsa_ssa_pss.sig_raw_len = bs_data_len;

out:
	return ret;
}

int parse_sig_rsa_9796_2_pad(sig_params *params,
			     const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 bs_data_start_off = 0, bs_data_len = 0;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((params == NULL) || (cert == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_sig_rsa_helper(buf, len,
				   &bs_data_start_off, &bs_data_len,
				   eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	params->rsa_9796_2_pad.sig_raw_off = off + bs_data_start_off;
	params->rsa_9796_2_pad.sig_raw_len = bs_data_len;

out:
	return ret;
}

int parse_sig_rsa_belgian(sig_params *params,
			  const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 bs_data_start_off = 0, bs_data_len = 0;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((params == NULL) || (cert == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_sig_rsa_helper(buf, len,
				   &bs_data_start_off, &bs_data_len,
				   eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	params->belgian_rsa.sig_raw_off = off + bs_data_start_off;
	params->belgian_rsa.sig_raw_len = bs_data_len;

out:
	return ret;
}

int parse_sig_dsa(sig_params *params,
		  const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 r_start_off = 0, r_len=0, s_start_off = 0, s_len = 0;
	const x509_u8 *buf = cert + off;
	int ret;

	if ((params == NULL) || (cert == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = sig_dsa_based_extract_r_s(buf, len, &r_start_off, &r_len,
					&s_start_off, &s_len, eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	params->dsa.r_raw_off = off + r_start_off;
	params->dsa.r_raw_len = r_len;
	params->dsa.s_raw_off = off + s_start_off;
	params->dsa.s_raw_len = s_len;
	ret = 0;

out:
	return ret;
}

static int parse_HashAlgorithm(const x509_u8 *buf, x509_u32 len, _hash_alg const **hash_alg,
			       x509_u32 *eaten)
{
	x509_u32 hdr_len = 0, data_len = 0, oid_len = 0, remain = 0;
	int ret;

	if ((buf == NULL) || (hash_alg == NULL) || (eaten == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	remain = data_len;

	ret = parse_OID(buf, remain, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*hash_alg = find_hash_by_oid(buf, oid_len);
	if (*hash_alg == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += oid_len;
	remain -= oid_len;

	if ((remain != 2) || (buf[0] != 0x05) || (buf[1] != 0x00)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = hdr_len + data_len;
	ret = 0;

out:
	return ret;
}

int parse_algoid_sig_params_rsassa_pss(sig_params *params, hash_alg_id *hash_alg,
					      const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	x509_u32 remain, hdr_len = 0, data_len = 0, oid_len = 0;
	x509_u32 int_hdr_len = 0, int_data_len = 0;
	x509_u32 attr_hdr_len = 0, attr_data_len = 0, eaten = 0;
	x509_u8 salt_len = 0;
	const x509_u8 *buf = cert + off;
	_hash_alg const *hash = NULL;
	_mgf const *mgf = NULL;
	_hash_alg const *mgf_hash = NULL;
	x509_u8 trailer_field = 0;
	int ret;

	if ((params == NULL) || (hash_alg == NULL) || (cert == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	remain = data_len;

	ret = parse_id_len(buf, remain, CLASS_CONTEXT_SPECIFIC, 0,
			   &attr_hdr_len, &attr_data_len);
	if (ret) {

		hash = &_sha1_hash_alg;
	} else {
		buf += attr_hdr_len;
		remain -= attr_hdr_len;

		ret = parse_HashAlgorithm(buf, attr_data_len, &hash, &eaten);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		if (eaten != attr_data_len) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += eaten;
		remain -= eaten;
	}

	*hash_alg = hash->hash_id;

	ret = parse_id_len(buf, remain, CLASS_CONTEXT_SPECIFIC, 1,
			   &attr_hdr_len, &attr_data_len);
	if (ret) {

		mgf = &_mgf1_alg;
		mgf_hash = &_sha1_hash_alg;
	} else {
		buf += attr_hdr_len;
		remain -= attr_hdr_len;

		ret = parse_id_len(buf, remain, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
				   &hdr_len, &data_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		if (attr_data_len != (hdr_len + data_len)) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += hdr_len;
		remain -= hdr_len;

		ret = parse_OID(buf, data_len, &oid_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		if ((oid_len != _mgf1_alg.alg_der_oid_len) ||
		    bufs_differ(buf, _mgf1_alg.alg_der_oid, oid_len)) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
		mgf = &_mgf1_alg;

		buf += oid_len;
		remain -= oid_len;
		data_len -= oid_len;

		ret = parse_HashAlgorithm(buf, data_len, &mgf_hash, &eaten);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += eaten;
		remain -= eaten;
		data_len -= eaten;

		if (data_len != 0) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

	}

	params->rsa_ssa_pss.mgf_alg = mgf->mgf_id;
	params->rsa_ssa_pss.mgf_hash_alg = mgf_hash->hash_id;

	ret = parse_id_len(buf, remain, CLASS_CONTEXT_SPECIFIC, 2,
			   &attr_hdr_len, &attr_data_len);
	if (ret) {

		salt_len = 20;
	} else {
		buf += attr_hdr_len;
		remain -= attr_hdr_len;

		ret = parse_non_negative_integer(buf, attr_data_len, CLASS_UNIVERSAL,
				ASN1_TYPE_INTEGER, &int_hdr_len,
				&int_data_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		eaten = int_hdr_len + int_data_len;
		if (eaten != attr_data_len) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		if (int_data_len != 1) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
		salt_len = buf[2];

		buf += eaten;
		remain -= eaten;
	}

	params->rsa_ssa_pss.salt_len = salt_len;

	ret = parse_id_len(buf, remain, CLASS_CONTEXT_SPECIFIC, 3,
			   &attr_hdr_len, &attr_data_len);
	if (ret) {

		trailer_field = 1; 
	} else {

		buf += attr_hdr_len;
		remain -= attr_hdr_len;

		ret = parse_integer(buf, attr_data_len, CLASS_UNIVERSAL,
				    ASN1_TYPE_INTEGER,
				    &int_hdr_len, &int_data_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		eaten = int_hdr_len + int_data_len;
		if (eaten != attr_data_len) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		if (eaten != 3) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		if (buf[2] == 1) {

			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	params->rsa_ssa_pss.trailer_field = trailer_field;

	if (remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

int parse_algoid_sig_params_ecdsa_with(sig_params *params, hash_alg_id *hash_alg,
					      const x509_u8 *cert, x509_u32 ATTRIBUTE_UNUSED off, x509_u32 len)
{
	int ret;

	if ((params == NULL) || (hash_alg == NULL) || (cert == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
	} else {
		ret = 0;
	}

out:
	return ret;
}

int parse_algoid_sig_params_ecdsa_with_specified(sig_params *params, hash_alg_id *hash_alg,
							const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	const x509_u8 *buf = cert + off;
	const _hash_alg *hash = NULL;
	x509_u32 parsed = 0;
	int ret;

	if ((params == NULL) || (hash_alg == NULL) || (cert == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_HashAlgorithm(buf, len, &hash, &parsed);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (parsed != len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*hash_alg = hash->hash_id;

out:
	return ret;
}

int parse_algoid_sig_params_bign_with_hspec(sig_params *params, hash_alg_id *hash_alg,
						   const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	const x509_u8 *buf = cert + off;
	const _hash_alg *hash;
	x509_u32 oid_len = 0;
	x509_u32 remain;
	int ret;

	if ((params == NULL) || (hash_alg == NULL) || (cert == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = len;

	ret = parse_OID(buf, remain, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	hash = find_hash_by_oid(buf, oid_len);
	if (hash == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += oid_len;
	remain -= oid_len;

	if (remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*hash_alg = hash->hash_id;
	ret = 0;

out:
	return ret;
}

int parse_algoid_sig_params_sm2(sig_params *params, hash_alg_id *hash_alg,
				       const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	const x509_u8 *buf = cert + off;
	x509_u32 parsed = 0;
	int ret;

	if ((params == NULL) || (hash_alg == NULL) || (cert == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	switch (len) {
	case 0:
		ret = 0;
		break;
	case 2:
		ret = parse_null(buf, len, &parsed);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		}
		break;
	default:
		ret = -X509_FILE_LINE_NUM_ERR;
		break;
	}

out:
	return ret;
}

int parse_algoid_sig_params_eddsa(sig_params *params, hash_alg_id *hash_alg,
					 const x509_u8 *cert, x509_u32 ATTRIBUTE_UNUSED off, x509_u32 len)
{
	int ret;

	if ((params == NULL) || (hash_alg == NULL) || (cert == NULL) || (len != 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

int parse_algoid_sig_params_none(sig_params *params, hash_alg_id *hash_alg,
					const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	int ret;

	if ((params == NULL) || (hash_alg == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_algoid_params_none(cert, off, len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

out:
	return ret;
}

int parse_algoid_params_none(const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	const x509_u8 *buf = cert + off;
	x509_u32 parsed = 0;
	int ret;

	if (cert == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	switch (len) {
	case 0: 
		ret = 0;
		break;
	case 2: 
		ret = parse_null(buf, len, &parsed);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		}
		break;
	default: 
		ret = -1;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		break;
	}

out:
	return ret;
}

int parse_algoid_params_rsa(const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	const x509_u8 *buf = cert + off;
	x509_u32 parsed = 0;
	int ret;

	if (cert == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

#ifdef TEMPORARY_LAXIST_RSA_PUBKEY_AND_SIG_NO_PARAMS_INSTEAD_OF_NULL

	if (len == 0) {
		ret = 0;
		goto out;
	}
#endif

	ret = parse_null(buf, len, &parsed);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

int parse_algoid_sig_params_rsa(sig_params *params, hash_alg_id *hash_alg,
				       const x509_u8 *cert, x509_u32 off, x509_u32 len)
{
	int ret;

	if ((cert == NULL) || (params == NULL) || (hash_alg == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_algoid_params_rsa(cert, off, len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

out:
	return ret;
}

x509_u64 time_components_to_comparable_u64(x509_u16 na_year, x509_u8 na_month, x509_u8 na_day,
				      x509_u8 na_hour, x509_u8 na_min, x509_u8 na_sec)
{
	x509_u64 res, tmp;

	res = ((x509_u64)na_sec);                 

	tmp = ((x509_u64)na_min) * (1ULL << 8);            

	res += tmp;

	tmp = (((x509_u64)na_hour) * (1ULL << 16));        

	res += tmp;

	tmp = ((x509_u64)na_day) * (1ULL << 24);           

	res += tmp;

	tmp = ((x509_u64)na_month) * (1ULL << 32);         

	res += tmp;

	tmp = ((x509_u64)na_year) * (1ULL << 40);         

	res += tmp;

	return res;
}

#undef X509_FILE_NUM
#define X509_FILE_NUM 5 

typedef enum {
	crl_reason_unspecified          = 0x00,
	crl_reason_keyCompromise        = 0x01,
	crl_reason_cACompromise         = 0x02,
	crl_reason_affiliationChanged   = 0x03,
	crl_reason_superseded           = 0x04,
	crl_reason_cessationOfOperation = 0x05,
	crl_reason_certificateHold      = 0x06,

	crl_reason_removeFromCRL        = 0x08,
	crl_reason_privilegeWithdrawn   = 0X09,
	crl_reason_aACompromise         = 0x0a
} crl_reason;

static const x509_u8 _crl_entry_ext_oid_ReasonCode[] =      { 0x06, 0x03, 0x55, 0x1d, 0x15 };
static const x509_u8 _crl_entry_ext_oid_InvalidityDate[] =  { 0x06, 0x03, 0x55, 0x1d, 0x18 };
static const x509_u8 _crl_entry_ext_oid_CertIssuer[] =      { 0x06, 0x03, 0x55, 0x1d, 0x1d };

static int parse_crl_entry_ext_ReasonCode(crl_parsing_ctx *ctx,
					  const x509_u8 *crl, x509_u32 off, x509_u32 len,
					  int critical)
{
	const x509_u8 *buf = crl + off;
	crl_reason reasonCode;
	int ret = -1;

	if ((ctx == NULL) || (crl == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (critical) {

		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if ((len != 3) || (buf[0] != ASN1_TYPE_ENUMERATED) || (buf[1] != 0x01)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	reasonCode = buf[2];
	switch (reasonCode) {
	case crl_reason_unspecified:
	case crl_reason_keyCompromise:
	case crl_reason_cACompromise:
	case crl_reason_affiliationChanged:
	case crl_reason_superseded:
	case crl_reason_cessationOfOperation:
	case crl_reason_certificateHold:
	case crl_reason_removeFromCRL:
	case crl_reason_privilegeWithdrawn:
	case crl_reason_aACompromise:
		ret = 0;
		break;

	default:
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

out:
	return ret;
}

static int parse_crl_entry_ext_InvalidityDate(crl_parsing_ctx *ctx,
					  const x509_u8 *crl, x509_u32 off, x509_u32 len,
					  int critical)
{
	x509_u8 month = 0, day = 0, hour = 0, min = 0, sec = 0;
	const x509_u8 *buf = crl + off;
	x509_u32 eaten;
	x509_u16 year;
	int ret = -1;

	if ((ctx == NULL) || (crl == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (critical) {

		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_generalizedTime(buf, len, &eaten,
				    &year, &month, &day, &hour, &min, &sec);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

out:
	return ret;
}

static int parse_crl_entry_ext_CertIssuer(crl_parsing_ctx *ctx,
					  const x509_u8 *crl, x509_u32 off, x509_u32 len,
					  int critical)
{
	const x509_u8 *buf = crl + off;
	x509_u32 eaten;
	int ret;

	if ((ctx == NULL) || (crl == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (!critical) {

		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_GeneralNames(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
				 &eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

out:
	return ret;
}

typedef struct {
	const x509_u8 *oid;
	x509_u8 oid_len;
	int (*parse_crl_entry_ext_params)(crl_parsing_ctx *ctx,
					  const x509_u8 *crl, x509_u32 off, x509_u32 len, int critical);
} _crl_entry_ext_oid;

static const _crl_entry_ext_oid known_crl_entry_ext_oids[] = {
	{ .oid = _crl_entry_ext_oid_ReasonCode, 
	  .oid_len = sizeof(_crl_entry_ext_oid_ReasonCode),
	  .parse_crl_entry_ext_params = parse_crl_entry_ext_ReasonCode,
	},
	{ .oid = _crl_entry_ext_oid_InvalidityDate, 
	  .oid_len = sizeof(_crl_entry_ext_oid_InvalidityDate),
	  .parse_crl_entry_ext_params = parse_crl_entry_ext_InvalidityDate,
	},
	{ .oid = _crl_entry_ext_oid_CertIssuer, 
	  .oid_len = sizeof(_crl_entry_ext_oid_CertIssuer),
	  .parse_crl_entry_ext_params = parse_crl_entry_ext_CertIssuer,
	},
};

#define NUM_KNOWN_CRL_ENTRY_EXT_OIDS (sizeof(known_crl_entry_ext_oids) /     \
				      sizeof(known_crl_entry_ext_oids[0]))
#define MAX_EXT_NUM_PER_CRL_ENTRY NUM_KNOWN_CRL_ENTRY_EXT_OIDS

static _crl_entry_ext_oid const * find_crl_entry_ext_by_oid(const x509_u8 *buf, x509_u32 len)
{
	const _crl_entry_ext_oid *found = NULL;
	const _crl_entry_ext_oid *cur = NULL;
	x509_u16 k;

	if ((buf == NULL) || (len == 0)) {
		goto out;
	}

	for (k = 0; k < NUM_KNOWN_CRL_ENTRY_EXT_OIDS; k++) {
		int ret;

		cur = &known_crl_entry_ext_oids[k];

		if (cur->oid_len != len) {
			continue;
		}

		ret = !bufs_differ(cur->oid, buf, cur->oid_len);
		if (ret) {
			found = cur;
			break;
		}
	}

out:
	return found;
}

static int check_record_crl_entry_ext_unknown(const _crl_entry_ext_oid *ext,
					      const _crl_entry_ext_oid **parsed_oid_list)
{
	x509_u16 pos = 0;
	int ret;

	while (pos < MAX_EXT_NUM_PER_CRL_ENTRY) {

		if (parsed_oid_list[pos] == NULL) {
			parsed_oid_list[pos] = ext;
			break;
		}

		if (ext == parsed_oid_list[pos]) {
			ret = -X509_FILE_LINE_NUM_ERR;
			goto out;
		}

		pos += 1;
	}

	if (pos >= MAX_EXT_NUM_PER_CRL_ENTRY) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

static int parse_x509_crl_entry_Extension(crl_parsing_ctx *ctx,
					  const x509_u8 *crl, x509_u32 off, x509_u32 len,
					  const _crl_entry_ext_oid **parsed_oid_list,
					  x509_u32 *eaten)
{
	x509_u32 ext_hdr_len = 0, ext_data_len = 0;
	x509_u32 hdr_len = 0, data_len = 0;
	x509_u32 saved_ext_len = 0, oid_len = 0;
	x509_u32 remain, parsed = 0;
	const x509_u8 *buf = crl + off;
	const _crl_entry_ext_oid *ext = NULL;
	int critical = 0;
	int ret;

	(void)parsed_oid_list;

	if ((crl == NULL) || (len == 0) || (ctx == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = len;

	ret = parse_id_len(buf, remain,
			   CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &ext_hdr_len, &ext_data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += ext_hdr_len;
	off += ext_hdr_len;
	remain -= ext_hdr_len;
	saved_ext_len = ext_hdr_len + ext_data_len;

	ret = parse_OID(buf, ext_data_len, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ext = find_crl_entry_ext_by_oid(buf, oid_len);
	if (ext == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = check_record_crl_entry_ext_unknown(ext, parsed_oid_list);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += oid_len;
	off += oid_len;
	ext_data_len -= oid_len;

	ret = parse_boolean(buf, ext_data_len, &parsed);
	if (ret) {

		if (ext_data_len && (buf[0] == ASN1_TYPE_BOOLEAN)) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
	} else {

		if (parsed != 3) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

#ifndef TEMPORARY_LAXIST_EXTENSION_CRITICAL_FLAG_BOOLEAN_EXPLICIT_FALSE
		if (buf[2] == 0x00) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
#endif

		critical = 1;

		buf += parsed;
		off += parsed;
		ext_data_len -= parsed;
	}

	ret = parse_id_len(buf, ext_data_len,
			   CLASS_UNIVERSAL, ASN1_TYPE_OCTET_STRING,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;
	ext_data_len -= hdr_len;

	if (data_len != ext_data_len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = ext->parse_crl_entry_ext_params(ctx, crl, off, ext_data_len, critical);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = saved_ext_len;
	ret = 0;

out:
	return ret;
}

static int parse_x509_crl_entry_Extensions(crl_parsing_ctx *ctx,
					   const x509_u8 *crl, x509_u32 off, x509_u32 len,
					   x509_u32 *eaten)
{
	x509_u32 data_len = 0, hdr_len = 0, remain = 0;
	const x509_u8 *buf = crl + off;
	x509_u32 saved_len = 0;
	const _crl_entry_ext_oid *parsed_crl_entry_ext_oid_list[MAX_EXT_NUM_PER_CRL_ENTRY];

	int ret;
	x509_u16 i;

	if ((crl == NULL) || (len == 0) || (ctx == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = data_len;
	buf += hdr_len;
	off += hdr_len;

	saved_len = hdr_len + data_len;

#ifndef TEMPORARY_LAXIST_ALLOW_CRL_ENTRY_EXT_WITH_EMPTY_SEQ

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
#endif

	for (i = 0; i < MAX_EXT_NUM_PER_CRL_ENTRY; i++) {
		parsed_crl_entry_ext_oid_list[i] = NULL;
	}

	while (remain) {
		x509_u32 ext_len = 0;

		ret = parse_x509_crl_entry_Extension(ctx, crl, off, remain,
						     parsed_crl_entry_ext_oid_list,
						     &ext_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		remain -= ext_len;
		buf += ext_len;
		off += ext_len;
	}

	*eaten = saved_len;

	ret = 0;

out:
	return ret;
}

static int parse_x509_crl_revokedCertificate(crl_parsing_ctx *ctx,
					     const x509_u8 *crl, x509_u32 off, x509_u32 len,
					     x509_u32 *eaten)
{
	const x509_u8 *buf = crl + off;
	x509_u32 rev_len = 0;
	x509_u16 rev_year = 0;
	x509_u8 rev_month = 0, rev_day = 0, rev_hour = 0, rev_min = 0, rev_sec = 0;
	x509_u8 t_type = 0;
	x509_u32 hdr_len = 0, data_len = 0;
	x509_u32 parsed = 0;
	x509_u32 remain = len;
	x509_u32 saved_rev_len = 0;
	int ret;

	if ((ctx == NULL) || (crl == NULL) || (len == 0) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = len;

	ret = parse_id_len(buf, remain,
			   CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	remain = data_len;
	off += hdr_len;
	saved_rev_len = hdr_len + data_len;

	ret = parse_SerialNumber(crl, off, remain, CLASS_UNIVERSAL,
				 ASN1_TYPE_INTEGER, &parsed);
	if (ret) {
	       ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
	       goto out;
	}

	buf += parsed;
	remain -= parsed;
	off += parsed;

	ret = parse_Time(buf, remain, &t_type, &rev_len, &rev_year, &rev_month,
			 &rev_day, &rev_hour, &rev_min, &rev_sec);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = verify_correct_time_use(t_type, rev_year);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += rev_len;
	remain -= rev_len;
	off += rev_len;

	if ((ctx->version != 0x01) && remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (remain) {
		ret = parse_x509_crl_entry_Extensions(ctx, crl, off, remain,
						      &parsed);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		remain -= parsed;
		off += parsed;
		buf += parsed;
	}

	*eaten = saved_rev_len;
	ret = 0;

out:
	return ret;
}

static int parse_crl_ext_AKI(crl_parsing_ctx *ctx,
			     const x509_u8 *crl, x509_u32 off, x509_u32 len,
			     int critical)
{
	x509_u32 hdr_len = 0, data_len = 0;
	const x509_u8 *buf = crl + off;
	x509_u32 key_id_hdr_len = 0, key_id_data_len = 0, key_id_data_off = 0;
	x509_u32 gen_names_off = 0, gen_names_len = 0;
	x509_u32 cert_serial_off = 0, cert_serial_len = 0;
	x509_u32 remain;
	x509_u32 parsed = 0;
	int ret, has_keyIdentifier = 0, has_gen_names_and_serial = 0;

	if ((crl == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (critical) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;
	remain = data_len;

	if (len != (hdr_len + data_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, remain, CLASS_CONTEXT_SPECIFIC, 0,
			   &key_id_hdr_len, &key_id_data_len);
	if (!ret) {

		if (!key_id_data_len) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		key_id_data_off = off + key_id_hdr_len;
		buf += key_id_hdr_len + key_id_data_len;
		off += key_id_hdr_len + key_id_data_len;
		remain -= key_id_hdr_len + key_id_data_len;
		has_keyIdentifier = 1;
	}

	ret = parse_GeneralNames(buf, remain, CLASS_CONTEXT_SPECIFIC, 1,
				 &parsed);
	if (!ret) {
		gen_names_off = off;
		gen_names_len = parsed;

		buf += parsed;
		off += parsed;
		remain -= parsed;

		ret = parse_AKICertSerialNumber(crl, off, remain,
						CLASS_CONTEXT_SPECIFIC, 2,
						&cert_serial_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		has_gen_names_and_serial = 1;
		cert_serial_off = off;

		buf += cert_serial_len;
		off += cert_serial_len;
		remain -= cert_serial_len;
	}

	if (remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->aki_has_keyIdentifier = has_keyIdentifier;

	if (ctx->aki_has_keyIdentifier) {
		ctx->aki_keyIdentifier_start = key_id_data_off;

		ctx->aki_keyIdentifier_len = key_id_data_len;

	}
	ctx->aki_has_generalNames_and_serial = has_gen_names_and_serial;

	if (ctx->aki_has_generalNames_and_serial) {
		ctx->aki_generalNames_start = gen_names_off;

		ctx->aki_generalNames_len = gen_names_len;

		ctx->aki_serial_start = cert_serial_off + 2;  

		ctx->aki_serial_len = cert_serial_len - 2;

	}
	ctx->has_aki = 1;

	ret = 0;

out:
	return ret;
}

static int parse_crl_ext_IAN(crl_parsing_ctx *ctx,
			     const x509_u8 *crl, x509_u32 off, x509_u32 len,
			     int ATTRIBUTE_UNUSED critical)
{
	x509_u32 data_len = 0, hdr_len = 0, remain = 0, eaten = 0;
	const x509_u8 *buf = crl + off;
	int ret, unused = 0;

	if ((crl == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (!data_len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;
	remain = data_len;

	if (len != (hdr_len + data_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	while (remain) {
		ret = parse_GeneralName(buf, remain, &eaten, &unused);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		remain -= eaten;
		off += eaten;
		buf += eaten;
	}

	ret = 0;

out:
	return ret;
}

#define MAX_CRL_EXT_NUM_LEN 22 

static int parse_crl_ext_CRLnum(crl_parsing_ctx *ctx,
				const x509_u8 *crl, x509_u32 off, x509_u32 len,
				int critical)
{
	const x509_u8 *buf = crl + off;
	x509_u32 parsed = 0, hdr_len = 0, data_len = 0;
	int ret;

	if ((crl == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (critical) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_integer(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_INTEGER,
			    &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	parsed = hdr_len + data_len;

	if (parsed > MAX_CRL_EXT_NUM_LEN) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (buf[2] & 0x80) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->has_crlnumber = 1;
	ctx->crlnumber_start = off + hdr_len;
	ctx->crlnumber_len = data_len;

	ret = 0;

out:
	return ret;
}

static int parse_crl_ext_DeltaCRL_indicator(crl_parsing_ctx *ctx,
					    const x509_u8 *crl, x509_u32 off, x509_u32 len,
					    int critical)
{
	const x509_u8 *buf = crl + off;
	x509_u32 parsed = 0, hdr_len = 0, data_len = 0;
	int ret;

	if ((crl == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (!critical) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_integer(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_INTEGER,
			    &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	parsed = hdr_len + data_len;

	if (parsed > MAX_CRL_EXT_NUM_LEN) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (buf[2] & 0x80) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	(void)ctx;

out:
	return ret;
}

static int parse_context_specific_boolean(const x509_u8 *buf, x509_u32 len, x509_u8 tag, x509_u32 *eaten)
{
	x509_u8 c, p, t;
	int ret;

	if ((buf == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len < 3) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	c = (buf[0] >> 6) & 0x03; 
	p = (buf[0] >> 5) & 0x01; 
	t = buf[0] & 0x1f;        

	if ((c != CLASS_CONTEXT_SPECIFIC) || (p != 0) || (t != tag)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (buf[1] != 0x01) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	switch (buf[2]) {
	case 0x00: 
	case 0xff: 
		break;
	default:
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
		break;
	}

	*eaten = 3;

	ret = 0;

out:
	return ret;
}

static int parse_crl_ext_IDP(crl_parsing_ctx *ctx,
			     const x509_u8 *crl, x509_u32 off, x509_u32 len,
			     int critical)
{
	x509_u32 remain, hdr_len = 0, data_len = 0, eaten = 0;
	int has_dp = 0;
	int onlyContainsUserCerts;
	int onlyContainsCACerts;
	int indirectCRL;
	int onlyContainsAttributeCerts;
	const x509_u8 *buf = crl + off;
	x509_u8 dpn_type = 0;
	int ret;

	if ((crl == NULL) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

#ifndef TEMPORARY_LAXIST_ALLOW_IDP_CRL_EXT_WITHOUT_CRITICAL_BIT_SET
	if (!critical) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
#else
	(void)critical;
#endif

	remain = len;

	ret = parse_id_len(buf, remain,
			   CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain -= hdr_len;
	buf += hdr_len;

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (remain != data_len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, remain, CLASS_CONTEXT_SPECIFIC, 0,
			   &hdr_len, &data_len);
	if (!ret) {
		x509_u32 dpn_remain = 0, dpn_eaten= 0;

		buf += hdr_len;
		remain -= hdr_len;
		dpn_remain = data_len;

		if (data_len == 0) {
			ret = -X509_FILE_LINE_NUM_ERR;
			goto out;
		}

		dpn_type = buf[0];

		switch (dpn_type) {
		case 0xa0: 
			ret = parse_GeneralNames(buf, dpn_remain,
						 CLASS_CONTEXT_SPECIFIC, 0,
						 &dpn_eaten);
			if (ret) {
				ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
				goto out;
			}

			dpn_remain -= dpn_eaten;
			buf += dpn_eaten;
			break;

		case 0xa1: 

			ret = -X509_FILE_LINE_NUM_ERR;
			goto out;
			break;

		default:
			ret = -X509_FILE_LINE_NUM_ERR;
			goto out;
			break;
		}

		if (dpn_remain) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		has_dp = 1;

		remain -= data_len;
	}

	onlyContainsUserCerts = 0;
	onlyContainsCACerts = 0;
	indirectCRL = 0;
	onlyContainsAttributeCerts = 0;

	ret = parse_context_specific_boolean(buf, remain, 0x01, &eaten);
	if (!ret) {

		if (buf[2] == 0x00) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		onlyContainsUserCerts = 1;

		remain -= eaten;
		buf += eaten;
	}

	ret = parse_context_specific_boolean(buf, remain, 0x02, &eaten);
	if (!ret) {

		if (buf[2] == 0x00) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		onlyContainsCACerts = 1;

		remain -= eaten;
		buf += eaten;
	}

	ret = parse_crldp_reasons(buf, remain, 0x03, &eaten);
	if (!ret) {
		buf += eaten;
		remain -= eaten;
	}

	ret = parse_context_specific_boolean(buf, remain, 0x04, &eaten);
	if (!ret) {

		if (buf[2] == 0x00) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		indirectCRL = 1;

		remain -= eaten;
		buf += eaten;
	}

	ret = parse_context_specific_boolean(buf, remain, 0x05, &eaten);
	if (!ret) {

		if (buf[2] == 0x00) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		onlyContainsAttributeCerts = 1;

		remain -= eaten;
		buf += eaten;
	}

	if ((onlyContainsUserCerts + onlyContainsCACerts + onlyContainsAttributeCerts) > 1) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

	(void)dpn_type;
	(void)has_dp;
	(void)indirectCRL;

out:
	return ret;
}

static int parse_crl_ext_CRLDP(crl_parsing_ctx *ctx,
			       const x509_u8 *crl, x509_u32 off, x509_u32 len,
			       int critical)
{
	int ret;

	(void)ctx;
	(void)crl;
	(void)off;
	(void)len;
	(void)critical;

	ret = -X509_FILE_LINE_NUM_ERR;
	ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
	return ret;
}

static int parse_crl_ext_expCertsOnCRL(crl_parsing_ctx *ctx,
				       const x509_u8 *crl, x509_u32 off, x509_u32 len,
				       int critical)
{
	x509_u8 month = 0, day = 0, hour = 0, min = 0, sec = 0;
	const x509_u8 *buf = crl + off;
	x509_u32 eaten;
	x509_u16 year;
	int ret = -1;

	if ((crl == NULL) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (critical) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_generalizedTime(buf, len, &eaten,
				    &year, &month, &day, &hour, &min, &sec);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

out:
	return ret;
}

static int parse_crl_ext_FreshestCRL(crl_parsing_ctx *ctx,
				     const x509_u8 *crl, x509_u32 off, x509_u32 len,
				     int critical)
{
	x509_u32 hdr_len = 0, data_len = 0, remain;
	const x509_u8 *buf = crl + off;
	int ret;

	if ((crl == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (critical) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len,
			   CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	remain = data_len;

	if (len != (hdr_len + data_len)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->has_crldp = 1;
	ctx->one_crldp_has_all_reasons = 0;

	while (remain) {
		int crldp_has_all_reasons = 0;
		x509_u32 eaten = 0;

		ret = parse_DistributionPoint(buf, remain,
					      &crldp_has_all_reasons, &eaten);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		if (crldp_has_all_reasons) {
			ctx->one_crldp_has_all_reasons = 1;
		}

		remain -= eaten;
		buf += eaten;
	}

	ret = 0;

out:
	return ret;
}

static int parse_crl_ext_AIA(crl_parsing_ctx ATTRIBUTE_UNUSED *ctx,
			     const x509_u8 *crl, x509_u32 off, x509_u32 len, int critical)
{
	int ret;

	if (ctx == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_AIA(crl, off, len, critical);
	if (ret) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

out:
	return ret;
}

static int parse_crl_ext_szOID_CRL_NEXT_PUBLISH(crl_parsing_ctx *ctx,
				     const x509_u8 *crl, x509_u32 off, x509_u32 len,
				     int ATTRIBUTE_UNUSED critical)
{
	x509_u8 t_month = 0, t_day = 0, t_hour = 0, t_min = 0, t_sec = 0;
	const x509_u8 *buf = crl + off;
	x509_u32 t_len = 0;
	x509_u16 t_year = 0;
	x509_u8 t_type = 0;
	int ret = -1;

	if ((ctx == NULL) || (crl == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_Time(buf, len, &t_type, &t_len, &t_year, &t_month,
			 &t_day, &t_hour, &t_min, &t_sec);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (t_len != len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

out:
	return ret;
}

static int parse_crl_ext_szOID_CERTSRV_CA_VERSION(crl_parsing_ctx *ctx,
						  const x509_u8 *crl, x509_u32 off, x509_u32 len,
						  int ATTRIBUTE_UNUSED critical)
{
	x509_u32 parsed = 0, hdr_len = 0, data_len = 0;
	const x509_u8 *buf = crl + off;
	int ret = -1;

	if ((ctx == NULL) || (crl == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_non_negative_integer(buf, len,
					 CLASS_UNIVERSAL, ASN1_TYPE_INTEGER,
					 &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (data_len > 4) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	parsed = hdr_len + data_len;
	if (parsed != len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	(void)critical;

out:
	return ret;
}

#define TEMPORARY_LAXIST_CRL_ALLOW_UNPARSED_MS_CRL_SELF_CDP

static int parse_crl_ext_szOID_CRL_SELF_CDP(crl_parsing_ctx *ctx,
				     const x509_u8 *crl, x509_u32 off, x509_u32 len,
				     int critical)
{
	(void)ctx;
	(void)crl;
	(void)off;
	(void)len;
	(void)critical;

#ifdef  TEMPORARY_LAXIST_CRL_ALLOW_UNPARSED_MS_CRL_SELF_CDP
	return 0;
#else
	return -1;
#endif
}

static const x509_u8 _crl_ext_oid_AKI[] =           { 0x06, 0x03, 0x55, 0x1d, 0x23 };
static const x509_u8 _crl_ext_oid_IAN[] =           { 0x06, 0x03, 0x55, 0x1d, 0x12 };
static const x509_u8 _crl_ext_oid_CRLnum[] =        { 0x06, 0x03, 0x55, 0x1d, 0x14 };
static const x509_u8 _crl_ext_oid_DeltaCRL[] =      { 0x06, 0x03, 0x55, 0x1d, 0x1b };
static const x509_u8 _crl_ext_oid_IDP[] =           { 0x06, 0x03, 0x55, 0x1d, 0x1c };
static const x509_u8 _crl_ext_oid_CRLDP[] =         { 0x06, 0x03, 0x55, 0x1d, 0x1f };
static const x509_u8 _crl_ext_oid_FreshestCRL[] =   { 0x06, 0x03, 0x55, 0x1d, 0x2e };
static const x509_u8 _crl_ext_oid_AIA[] =           { 0x06, 0x08, 0x2b, 0x06, 0x01,
						 0x05, 0x05, 0x07, 0x01, 0x01 };

static const x509_u8 _crl_ext_oid_expCertsOnCRL[] = { 0x06, 0x03, 0x55, 0x1d, 0x3c };

static const x509_u8 _crl_ext_oid_szOID_CRL_NEXT_PUBLISH[] = {
	0x06, 0x09, 0x2b, 0x06,	0x01, 0x04, 0x01, 0x82, 0x37, 0x15, 0x04 };
static const x509_u8 _crl_ext_oid_szOID_CERTSRV_CA_VERSION[] = {
	0x06, 0x09, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x15, 0x01 };
static const x509_u8 _crl_ext_oid_szOID_CRL_SELF_CDP[] = {
	0x06, 0x09, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x15, 0x0e };

typedef struct {
	const x509_u8 *oid;
	x509_u8 oid_len;
	int (*parse_crl_ext_params)(crl_parsing_ctx *ctx,
				    const x509_u8 *crl, x509_u32 off, x509_u32 len, int critical);
} _crl_ext_oid;

static const _crl_ext_oid known_crl_ext_oids[] = {
	{ .oid = _crl_ext_oid_AKI, 
	  .oid_len = sizeof(_crl_ext_oid_AKI),
	  .parse_crl_ext_params = parse_crl_ext_AKI,
	},
	{ .oid = _crl_ext_oid_IAN, 
	  .oid_len = sizeof(_crl_ext_oid_IAN),
	  .parse_crl_ext_params = parse_crl_ext_IAN,
	},
	{ .oid = _crl_ext_oid_CRLnum, 
	  .oid_len = sizeof(_crl_ext_oid_CRLnum),
	  .parse_crl_ext_params = parse_crl_ext_CRLnum,
	},
	{ .oid = _crl_ext_oid_DeltaCRL, 
	  .oid_len = sizeof(_crl_ext_oid_DeltaCRL),
	  .parse_crl_ext_params = parse_crl_ext_DeltaCRL_indicator,
	},
	{ .oid = _crl_ext_oid_IDP, 
	  .oid_len = sizeof(_crl_ext_oid_IDP),
	  .parse_crl_ext_params = parse_crl_ext_IDP,
	},
	{ .oid = _crl_ext_oid_CRLDP, 
	  .oid_len = sizeof(_crl_ext_oid_CRLDP),
	  .parse_crl_ext_params = parse_crl_ext_CRLDP,
	},
	{ .oid = _crl_ext_oid_FreshestCRL, 
	  .oid_len = sizeof(_crl_ext_oid_FreshestCRL),
	  .parse_crl_ext_params = parse_crl_ext_FreshestCRL,
	},
	{ .oid = _crl_ext_oid_AIA, 
	  .oid_len = sizeof(_crl_ext_oid_AIA),
	  .parse_crl_ext_params = parse_crl_ext_AIA,
	},
	{ .oid = _crl_ext_oid_expCertsOnCRL, 
	  .oid_len = sizeof(_crl_ext_oid_expCertsOnCRL),
	  .parse_crl_ext_params = parse_crl_ext_expCertsOnCRL,
	},
	{ .oid = _crl_ext_oid_szOID_CRL_NEXT_PUBLISH, 
	  .oid_len = sizeof(_crl_ext_oid_szOID_CRL_NEXT_PUBLISH),
	  .parse_crl_ext_params = parse_crl_ext_szOID_CRL_NEXT_PUBLISH,
	},
	{ .oid = _crl_ext_oid_szOID_CERTSRV_CA_VERSION, 
	  .oid_len = sizeof(_crl_ext_oid_szOID_CERTSRV_CA_VERSION),
	  .parse_crl_ext_params = parse_crl_ext_szOID_CERTSRV_CA_VERSION,
	},
	{ .oid = _crl_ext_oid_szOID_CRL_SELF_CDP, 
	  .oid_len = sizeof(_crl_ext_oid_szOID_CRL_SELF_CDP),
	  .parse_crl_ext_params = parse_crl_ext_szOID_CRL_SELF_CDP,
	},
};

#define NUM_KNOWN_CRL_EXT_OIDS (sizeof(known_crl_ext_oids) /       \
				sizeof(known_crl_ext_oids[0]))
#define MAX_EXT_NUM_PER_CRL NUM_KNOWN_CRL_EXT_OIDS

static _crl_ext_oid const * find_crl_ext_by_oid(const x509_u8 *buf, x509_u32 len)
{
	const _crl_ext_oid *found = NULL;
	const _crl_ext_oid *cur = NULL;
	x509_u16 k;

	if ((buf == NULL) || (len == 0)) {
		goto out;
	}

	for (k = 0; k < NUM_KNOWN_CRL_EXT_OIDS; k++) {
		int ret;

		cur = &known_crl_ext_oids[k];

		if (cur->oid_len != len) {
			continue;
		}

		ret = !bufs_differ(cur->oid, buf, cur->oid_len);
		if (ret) {
			found = cur;
			break;
		}
	}

out:
	return found;
}

static int check_record_crl_ext_unknown(const _crl_ext_oid *ext,
					const _crl_ext_oid **parsed_oid_list)
{
	x509_u16 pos = 0;
	int ret;

	while (pos < MAX_EXT_NUM_PER_CRL) {

		if (parsed_oid_list[pos] == NULL) {
			parsed_oid_list[pos] = ext;
			break;
		}

		if (ext == parsed_oid_list[pos]) {
			ret = -X509_FILE_LINE_NUM_ERR;
			goto out;
		}

		pos += 1;
	}

	if (pos >= MAX_EXT_NUM_PER_CRL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

static int parse_x509_crl_Extension(crl_parsing_ctx *ctx,
				    const x509_u8 *crl, x509_u32 off, x509_u32 len,
				    const _crl_ext_oid **parsed_oid_list,
				    x509_u32 *eaten)
{
	x509_u32 ext_hdr_len = 0, ext_data_len = 0;
	x509_u32 hdr_len = 0, data_len = 0;
	x509_u32 saved_ext_len = 0, oid_len = 0;
	x509_u32 remain, parsed = 0;
	const x509_u8 *buf = crl + off;
	const _crl_ext_oid *ext = NULL;
	int critical = 0;
	int ret;

	if ((crl == NULL) || (len == 0) || (ctx == NULL) || (eaten == NULL) ||
	    (parsed_oid_list == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = len;

	ret = parse_id_len(buf, remain,
			   CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &ext_hdr_len, &ext_data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += ext_hdr_len;
	off += ext_hdr_len;
	remain -= ext_hdr_len;
	saved_ext_len = ext_hdr_len + ext_data_len;

	ret = parse_OID(buf, ext_data_len, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ext = find_crl_ext_by_oid(buf, oid_len);
	if (ext == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = check_record_crl_ext_unknown(ext, parsed_oid_list);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += oid_len;
	off += oid_len;
	ext_data_len -= oid_len;

	ret = parse_boolean(buf, ext_data_len, &parsed);
	if (ret) {

		if (ext_data_len && (buf[0] == ASN1_TYPE_BOOLEAN)) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
	} else {

		if (parsed != 3) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

#ifndef TEMPORARY_LAXIST_EXTENSION_CRITICAL_FLAG_BOOLEAN_EXPLICIT_FALSE
		if (buf[2] == 0x00) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}
#endif

		critical = (buf[2] == 0) ? 0 : 1;

		buf += parsed;
		off += parsed;
		ext_data_len -= parsed;
	}

	ret = parse_id_len(buf, ext_data_len,
			   CLASS_UNIVERSAL, ASN1_TYPE_OCTET_STRING,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;
	off += hdr_len;
	ext_data_len -= hdr_len;

	if (data_len != ext_data_len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = ext->parse_crl_ext_params(ctx, crl, off, ext_data_len, critical);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = saved_ext_len;
	ret = 0;

out:
	return ret;
}

static int parse_x509_crl_Extensions(crl_parsing_ctx *ctx,
				     const x509_u8 *crl, x509_u32 off, x509_u32 len,
				     x509_u32 *eaten)
{
	x509_u32 data_len = 0, hdr_len = 0, remain = 0;
	const x509_u8 *buf = crl + off;
	x509_u32 saved_len = 0;
	const _crl_ext_oid *parsed_crl_ext_oid_list[MAX_EXT_NUM_PER_CRL];

	int ret;
	x509_u16 i;

	if ((crl == NULL) || (len == 0) || (ctx == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_explicit_id_len(buf, len, 0,
				    CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
				    &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain = data_len;
	buf += hdr_len;
	off += hdr_len;

	saved_len = hdr_len + data_len;

	if (data_len == 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	for (i = 0; i < MAX_EXT_NUM_PER_CRL; i++) {
		parsed_crl_ext_oid_list[i] = NULL;
	}

	while (remain) {
		x509_u32 ext_len = 0;

		ret = parse_x509_crl_Extension(ctx, crl, off, remain,
					       parsed_crl_ext_oid_list,
					       &ext_len);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		remain -= ext_len;
		buf += ext_len;
		off += ext_len;
	}

#ifndef TEMPORARY_LAXIST_ALLOW_MISSING_AKI_OR_CRLNUM
	if (!(ctx->has_crlnumber && ctx->has_aki)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
#endif

	*eaten = saved_len;

	ret = 0;

out:
	return ret;
}

static int parse_x509_crl_Version(const x509_u8 *cert, x509_u32 off, x509_u32 len, x509_u8 *version, x509_u32 *eaten)
{
	const x509_u8 *buf = cert + off;
	x509_u32 data_len = 0;
	x509_u32 hdr_len = 0;
	int ret;

	if ((cert == NULL) || (version == NULL) || (len == 0) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_integer(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_INTEGER,
			    &hdr_len, &data_len);
	if (ret) {
		ret = X509_PARSER_ERROR_VERSION_ABSENT;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += hdr_len;

	if (data_len != 1) {
		ret = X509_PARSER_ERROR_VERSION_UNEXPECTED_LENGTH;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*version = buf[0];
	*eaten = hdr_len + data_len;

	ret = 0;

out:
	return ret;
}

static int parse_x509_tbsCertList_sig_AlgorithmIdentifier(crl_parsing_ctx *ctx,
							  const x509_u8 *crl, x509_u32 off, x509_u32 len,
							  const _sig_alg **alg,
							  x509_u32 *eaten)
{
	const _sig_alg *talg = NULL;
	const x509_u8 *buf = crl + off;
	x509_u32 saved_off = off;
	x509_u32 parsed = 0;
	x509_u32 hdr_len = 0;
	x509_u32 data_len = 0;
	x509_u32 param_len;
	x509_u32 oid_len = 0;
	int ret;

	if ((ctx == NULL) || (crl == NULL) || (len == 0)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;}

	parsed = hdr_len + data_len;

	buf += hdr_len;
	off += hdr_len;

	ret = parse_OID(buf, data_len, &oid_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	talg = find_sig_alg_by_oid(buf, oid_len);
	if (talg == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->tbs_sig_alg_oid_start = off;
	ctx->tbs_sig_alg_oid_len = oid_len;
	ctx->sig_alg = talg->sig_id;
	ctx->hash_alg = talg->hash_id;

	buf += oid_len;
	off += oid_len;
	param_len = data_len - oid_len;

	ret = talg->parse_algoid_sig_params(&ctx->sig_alg_params,
					    &ctx->hash_alg, crl, off, param_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*alg = talg;
	*eaten = parsed;
	ctx->tbs_sig_alg_start = saved_off;
	ctx->tbs_sig_alg_len = parsed;

	ret = 0;

out:
	return ret;
}

static int parse_x509_crl_revokedCertificates(crl_parsing_ctx *ctx,
					      const x509_u8 *crl, x509_u32 off, x509_u32 len,
					      x509_u32 *eaten)
{
	x509_u32 remain = 0, hdr_len = 0, data_len = 0, parsed = 0, cur_off = 0;
	const x509_u8 *buf = crl + off;
	int ret;

	if ((ctx == NULL) || (crl == NULL) || (len == 0) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ctx->has_revoked_certs = 0;
		*eaten = 0;
		ret = 0;
		goto out;
	}

	remain = data_len;
	cur_off = off + hdr_len;

#ifndef TEMPORARY_LAXIST_ALLOW_REVOKED_CERTS_LIST_EMPTY
	if (data_len == 0) {

		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
#endif

	while (remain) {
		ret = parse_x509_crl_revokedCertificate(ctx, crl, cur_off,
							remain,
							&parsed);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		remain -= parsed;
		cur_off += parsed;
	}

	ctx->has_revoked_certs = data_len != 0;
	*eaten = data_len + hdr_len;
	ret = 0;

out:
	return ret;
}

static int parse_x509_TBSCertList(crl_parsing_ctx *ctx,
			     const x509_u8 *crl, x509_u32 off, x509_u32 len,
			     const _sig_alg **sig_alg, x509_u32 *eaten)
{
	x509_u32 tbs_data_len = 0;
	x509_u32 tbs_hdr_len = 0;
	x509_u32 tbs_crl_len = 0;
	x509_u32 remain = 0;
	x509_u32 parsed = 0;
	x509_u32 cur_off = off;
	const x509_u8 *buf = crl + cur_off;
	const _sig_alg *alg = NULL;
	int ret, empty_issuer = 1;
	x509_u32 tu_len = 0, nu_len = 0;
	x509_u16 nu_year = 0, tu_year = 0;
	x509_u8 nu_month = 0, nu_day = 0, nu_hour = 0, nu_min = 0, nu_sec = 0;
	x509_u8 tu_month = 0, tu_day = 0, tu_hour = 0, tu_min = 0, tu_sec = 0;
	x509_u64 thisUpdate, nextUpdate;
	x509_u8 t_type = 0;

	if ((ctx == NULL) || (crl == NULL) || (len == 0) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &tbs_hdr_len, &tbs_data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	tbs_crl_len = tbs_hdr_len + tbs_data_len;
	buf += tbs_hdr_len;
	cur_off += tbs_hdr_len;
	remain = tbs_data_len;

	ret = parse_x509_crl_Version(crl, cur_off, remain,
				     &ctx->version, &parsed);
	if (ret) {

		ctx->version = 0x00;
	} else {

		if (ctx->version != 0x01) {
			ret = -X509_FILE_LINE_NUM_ERR;
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += parsed;
		cur_off += parsed;
		remain -= parsed;
	}

	ret = parse_x509_tbsCertList_sig_AlgorithmIdentifier(ctx, crl, cur_off,
							     remain, &alg,
							     &parsed);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += parsed;
	cur_off += parsed;
	remain -= parsed;

	ret = parse_x509_Name(buf, remain, &parsed, &empty_issuer);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	ctx->issuer_start = cur_off;
	ctx->issuer_len = parsed;

	if (empty_issuer) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	buf += parsed;
	cur_off += parsed;
	remain -= parsed;

	ret = parse_Time(buf, remain, &t_type, &tu_len, &tu_year, &tu_month,
			 &tu_day, &tu_hour, &tu_min, &tu_sec);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = verify_correct_time_use(t_type, tu_year);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	remain -= tu_len;
	cur_off += tu_len;
	buf += tu_len;

	thisUpdate = time_components_to_comparable_u64(tu_year, tu_month, tu_day,
							tu_hour, tu_min, tu_sec);

	ret = parse_Time(buf, remain, &t_type, &nu_len, &nu_year, &nu_month,
			 &nu_day, &nu_hour, &nu_min, &nu_sec);
	if (!ret) {

		ret = verify_correct_time_use(t_type, nu_year);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		remain -= nu_len;
		cur_off += nu_len;
		buf += nu_len;
	} else {
#ifdef TEMPORARY_LAXIST_ALLOW_MISSING_CRL_NEXT_UPDATE

		nu_year = 9999;
		nu_month = 12;
		nu_day = 31;
		nu_hour = 23;
		nu_min = 59;
		nu_sec = 59;
#else

		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
#endif
	}

	nextUpdate = time_components_to_comparable_u64(nu_year, nu_month, nu_day,
						       nu_hour, nu_min, nu_sec);

	if (thisUpdate >= nextUpdate) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (remain) {
		ret = parse_x509_crl_revokedCertificates(ctx, crl, cur_off, remain,
							 &parsed);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		cur_off += parsed;
		remain -= parsed;

	}

	if ((ctx->version != 0x01) && remain) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (remain) {
		ret = parse_x509_crl_Extensions(ctx, crl, cur_off, remain,
						&parsed);
		if (ret) {
			ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
			goto out;
		}

		buf += parsed;
		cur_off += parsed;
		remain -= parsed;
	}

	if (remain != 0) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = tbs_crl_len;

	*sig_alg = alg;

	ret = 0;

out:
	return ret;
}

static int parse_x509_crl_signatureAlgorithm(crl_parsing_ctx *ctx,
					    const x509_u8 *crl, x509_u32 off, x509_u32 len,
					    x509_u32 *eaten)
{
	x509_u32 prev_len;
	int ret;

	if ((crl == NULL) || (len == 0) || (ctx == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	prev_len = ctx->tbs_sig_alg_len;
	if (prev_len > len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = bufs_differ(crl + ctx->tbs_sig_alg_start, crl + off, prev_len);
	if (ret) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}
	ctx->sig_alg_start = off;
	ctx->sig_alg_len = prev_len;

	*eaten = prev_len;

	ret = 0;

out:
	return ret;
}

static int parse_x509_crl_signatureValue(crl_parsing_ctx *ctx,
					const x509_u8 *crl, x509_u32 off, x509_u32 len,
					const _sig_alg *sig_alg, x509_u32 *eaten)
{
	x509_u32 saved_off = off;
	int ret;

	if ((ctx == NULL) || (crl == NULL) || (len == 0) ||
	    (sig_alg == NULL) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (sig_alg->parse_sig == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = sig_alg->parse_sig(&(ctx->sig_alg_params), crl, off, len, eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->sig_start = saved_off;
	ctx->sig_len = *eaten;
	ret = 0;

out:
	return ret;
}

static crl_parsing_ctx get_zeroized_crl_ctx_val(void)
{
	crl_parsing_ctx zeroized_ctx = { 0 };

	return zeroized_ctx;
}

int parse_x509_crl(crl_parsing_ctx *ctx, const x509_u8 *crl, x509_u32 len)
{
	x509_u32 seq_data_len = 0;
	x509_u32 eaten = 0;
	x509_u32 off = 0;
	const _sig_alg *sig_alg = NULL;
	int ret;

	if ((crl == NULL) || (len == 0) || (ctx == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*ctx = get_zeroized_crl_ctx_val();

	ret = parse_id_len(crl, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &eaten, &seq_data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	len -= eaten;
	off += eaten;

	if (seq_data_len != len) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_x509_TBSCertList(ctx, crl, off, len, &sig_alg, &eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ctx->tbs_start = off;
	ctx->tbs_len = eaten;

	len -= eaten;
	off += eaten;

	ret = parse_x509_crl_signatureAlgorithm(ctx, crl, off, len, &eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	len -= eaten;
	off += eaten;

	ret = parse_x509_crl_signatureValue(ctx, crl, off, len, sig_alg, &eaten);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len != eaten) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

#undef X509_FILE_NUM
#define X509_FILE_NUM 4 

int parse_x509_cert_relaxed(cert_parsing_ctx *ctx, const x509_u8 *buf, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 hdr_len = 0, data_len = 0;
	int ret;

	if ((ctx == NULL) || (buf == NULL) || (len == 0) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ret = 1;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = hdr_len + data_len;

	ret = parse_x509_cert(ctx, buf, hdr_len + data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

int parse_x509_crl_relaxed(crl_parsing_ctx *ctx, const x509_u8 *buf, x509_u32 len, x509_u32 *eaten)
{
	x509_u32 hdr_len = 0, data_len = 0;
	int ret;

	if ((ctx == NULL) || (buf == NULL) || (len == 0) || (eaten == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = parse_id_len(buf, len, CLASS_UNIVERSAL, ASN1_TYPE_SEQUENCE,
			   &hdr_len, &data_len);
	if (ret) {
		ret = 1;
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	*eaten = hdr_len + data_len;

	ret = parse_x509_crl(ctx, buf, hdr_len + data_len);
	if (ret) {
		ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;

out:
	return ret;
}

#undef X509_FILE_NUM
#define X509_FILE_NUM 0 

int bufs_differ(const x509_u8 *b1, const x509_u8 *b2, x509_u32 n)
{
	int ret = 0;
	x509_u32 i = 0;

	for (i = 0; i < n; i++) {
		if(b1[i] != b2[i]) {
			ret = 1;
			break;
		}
	}

	return ret;
}
