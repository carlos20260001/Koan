/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */

#include <LELegacyPairingCrypto.h>

#include <string.h>

extern "C" {
// The OpenBSD AES from src/libs/compat is kernel code (sys/systm.h); on
// Linux the block cipher comes from OpenSSL, which be links already.
#include <openssl/evp.h>
}


namespace Bluetooth {

static void
ClearSecret(void* data, size_t size)
{
	volatile uint8_t* bytes = (volatile uint8_t*)data;
	while (size-- > 0)
		*bytes++ = 0;
}


static bool
EncryptLittleEndian(const uint8_t key[16], const uint8_t input[16],
	uint8_t output[16])
{
	uint8_t reversedKey[16];
	uint8_t reversedInput[16];
	uint8_t encrypted[16];
	for (size_t i = 0; i < 16; i++) {
		reversedKey[i] = key[15 - i];
		reversedInput[i] = input[15 - i];
	}
	// One AES-128 block: ECB without padding.
	EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
	int length = 0;
	bool okay = context != NULL
		&& EVP_EncryptInit_ex(context, EVP_aes_128_ecb(), NULL, reversedKey,
			NULL) == 1
		&& EVP_CIPHER_CTX_set_padding(context, 0) == 1
		&& EVP_EncryptUpdate(context, encrypted, &length, reversedInput, 16)
			== 1
		&& length == 16;
	if (okay) {
		for (size_t i = 0; i < 16; i++)
			output[i] = encrypted[15 - i];
	}
	EVP_CIPHER_CTX_free(context);
	ClearSecret(reversedKey, sizeof(reversedKey));
	ClearSecret(reversedInput, sizeof(reversedInput));
	ClearSecret(encrypted, sizeof(encrypted));
	return okay;
}


bool
LELegacyConfirm(const uint8_t tk[16], const uint8_t random[16],
	const uint8_t request[7], const uint8_t response[7],
	uint8_t initiatorAddressType, const uint8_t initiatorAddress[6],
	uint8_t responderAddressType, const uint8_t responderAddress[6],
	uint8_t confirm[16])
{
	if (tk == NULL || random == NULL || request == NULL || response == NULL
		|| initiatorAddress == NULL || responderAddress == NULL
		|| confirm == NULL || initiatorAddressType > 1
		|| responderAddressType > 1)
		return false;

	uint8_t p1[16] = { initiatorAddressType, responderAddressType };
	memcpy(p1 + 2, request, 7);
	memcpy(p1 + 9, response, 7);
	uint8_t p2[16] = {};
	memcpy(p2, responderAddress, 6);
	memcpy(p2 + 6, initiatorAddress, 6);
	uint8_t intermediate[16];
	for (size_t i = 0; i < 16; i++)
		intermediate[i] = random[i] ^ p1[i];
	bool okay = EncryptLittleEndian(tk, intermediate, intermediate);
	if (okay) {
		for (size_t i = 0; i < 16; i++)
			intermediate[i] ^= p2[i];
		okay = EncryptLittleEndian(tk, intermediate, confirm);
	}
	ClearSecret(intermediate, sizeof(intermediate));
	return okay;
}


bool
LELegacyShortTermKey(const uint8_t tk[16],
	const uint8_t firstRandom[16], const uint8_t secondRandom[16],
	uint8_t key[16])
{
	if (tk == NULL || firstRandom == NULL || secondRandom == NULL
		|| key == NULL)
		return false;
	uint8_t input[16];
	memcpy(input, secondRandom, 8);
	memcpy(input + 8, firstRandom, 8);
	bool okay = EncryptLittleEndian(tk, input, key);
	ClearSecret(input, sizeof(input));
	return okay;
}


bool
LEResolvePrivateAddress(const uint8_t irk[16], const uint8_t address[6])
{
	if (irk == NULL || address == NULL || (address[5] & 0xc0) != 0x40)
		return false;
	uint8_t nonzero = 0;
	for (size_t i = 0; i < 16; i++)
		nonzero |= irk[i];
	if (nonzero == 0)
		return false;
	// ah(IRK, prand) = e(IRK, 0x000... || prand) mod 2^24.
	uint8_t input[16] = {};
	memcpy(input, address + 3, 3);
	uint8_t encrypted[16] = {};
	bool okay = EncryptLittleEndian(irk, input, encrypted);
	uint8_t difference = 0;
	for (size_t i = 0; i < 3; i++)
		difference |= encrypted[i] ^ address[i];
	ClearSecret(encrypted, sizeof(encrypted));
	return okay && difference == 0;
}

} // namespace Bluetooth
