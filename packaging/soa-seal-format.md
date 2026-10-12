# SOA Seal v1

SOA Seal v1 is what the Story of Alicia launcher uses to sign and verify update metadata.

It uses standard Ed25519 signatures, but with a few extra things to make sure a signature meant for launcher updates can't be reused for something else.

## Key files

To generate a new key, run:

```bash
./packaging/create-update-key.sh /path/to/private-directory
```

This creates three files:

```text
soa-update-key.pem
soa-update-public-key.hex
soa-update-key-id.txt
```

`soa-update-key.pem` is the private key and must be kept private.

The public key and key ID can be shared.

The launcher uses the 64-character public key from:

```text
packaging/soa-update-public-key.hex
```

This key is compiled into the launcher.

## Key ID

Each public key has its own ID, which looks something like this:

```text
SOA1-ABCD-1234-5678-90AB-CDEF-1234-5678-90AB
```

The ID is generated using:

```text
hash = SHA256("SOA-SEAL-KEY-ID-V1\0" || raw_ed25519_public_key)
key_id = "SOA1-" || uppercase_hex(hash[0:16]), grouped every 4 hex characters
```

The key ID isn't secret, and it doesn't replace the public key. It's included in every SOA Seal signature so the launcher can check that the correct key was used.

## Seal files

Each signed metadata file has a `.seal` file alongside it.

A seal contains exactly four lines:

```text
SOA-SEAL-V1
kind=manifest
key=SOA1-ABCD-1234-5678-90AB-CDEF-1234-5678-90AB
sig=BASE64_ED25519_SIGNATURE
```

The signature is a standard 64-byte Ed25519 signature encoded in base64.

There are currently two supported document types:

```text
manifest  manifest.json
history   versions.json
```

## What gets signed

The signature is generated from these exact bytes:

```text
"SOA-SEAL-V1\0"
|| "kind=" || kind || "\0"
|| "key=" || key_id || "\0"
|| exact_document_bytes
```

The JSON files are signed exactly as they are. Nothing is reformatted, parsed, or changed before signing.

This also means changing even one byte in the JSON will invalidate its signature.

The document type and key ID are included in the signed data too. This prevents a signature for `manifest.json` from being reused for `versions.json`, or someone changing the key ID without invalidating the signature.

## Verification

Before accepting update metadata, the launcher checks that:

1. The seal has the correct `SOA-SEAL-V1` format with exactly four lines.
2. The document type matches the file being downloaded.
3. The key ID uses the correct `SOA1-...` format.
4. The key ID matches the one generated from the public key compiled into the launcher.
5. The base64 signature decodes to exactly 64 bytes.
6. The Ed25519 signature is valid for the signed data.

If any of these checks fail, the launcher rejects the metadata.

The launcher only accepts SOA Seal v1 for update metadata. Regular Ed25519 signatures without the SOA Seal format are not supported.
