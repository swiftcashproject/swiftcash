#!/bin/bash

INPUT="forkdrops"
OUTPUT="data"
READABLE="data_readable"

API="https://explorer.swiftcash.cc"

# SwiftCash P2PKH address version
PUBKEY_ADDRESS_VERSION=63

# Reference point:
# Block 339745 = 2026-09-20
REFERENCE_BLOCK=339745
REFERENCE_DATE="2026-09-20"

# Current UTC date
TODAY=$(date -u +%Y-%m-%d)
TODAY_TIMESTAMP=$(date -u -d "$TODAY" +%s)

TEMP=$(mktemp)

trap 'rm -f "$TEMP"' EXIT


#
# Convert HASH160 to a SwiftCash legacy P2PKH address.
#
hash160_to_address()
{
    local hash160="$1"

    python3 - "$hash160" "$PUBKEY_ADDRESS_VERSION" <<'PY'
import sys
import hashlib

hash160 = sys.argv[1]
version = int(sys.argv[2])

payload = bytes([version]) + bytes.fromhex(hash160)

checksum = hashlib.sha256(
    hashlib.sha256(payload).digest()
).digest()[:4]

data = payload + checksum

alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"

n = int.from_bytes(data, "big")
result = ""

while n:
    n, remainder = divmod(n, 58)
    result = alphabet[remainder] + result

for byte in data:
    if byte == 0:
        result = "1" + result
    else:
        break

print(result)
PY
}


#
# Convert SWIFT amount to exact 18-decimal raw units.
#
amount_to_raw()
{
    local amount="$1"

    python3 - "$amount" <<'PY'
import sys
from decimal import Decimal

amount = Decimal(sys.argv[1])
raw = amount * Decimal(10**18)

if raw != raw.to_integral_value():
    raise SystemExit("ERROR: Amount has more than 18 decimal places")

print(int(raw))
PY
}


> "$OUTPUT"
> "$READABLE"


#
# Input format:
#
# address amount
#
# The index is generated automatically starting from 1.
#
source_index=1

while read -r address amount; do

    echo "Processing $source_index $address $amount"

    #
    # Convert source amount exactly to 18-decimal raw units.
    #
    amount_raw=$(amount_to_raw "$amount")

    if [ $? -ne 0 ] || [ -z "$amount_raw" ]; then
        echo "ERROR: Could not convert amount $amount for $address" >&2
        source_index=$((source_index + 1))
        continue
    fi


    #
    # Normal S... address
    #
    if [[ "$address" == S* ]]; then

        scriptpubkey=$(swiftcash-cli validateaddress "$address" | jq -r '.scriptPubKey')

        if [ "$scriptpubkey" = "null" ] || [ -z "$scriptpubkey" ]; then
            echo "ERROR: No scriptPubKey found for $address" >&2
            source_index=$((source_index + 1))
            continue
        fi

        #
        # P2PKH:
        # 76a914 + HASH160 + 88ac
        #
        hash160="${scriptpubkey:6:40}"

        #
        # Normal addresses are immediately unlocked.
        #
        unlock_timestamp="$TODAY_TIMESTAMP"

        #
        # Store RAW balance.
        #
        printf '%s\t%s\t%s\t%s\n' \
            "$hash160" \
            "$amount_raw" \
            "$unlock_timestamp" \
            "$address" >> "$TEMP"


    #
    # Timelocked 8... address
    #
    elif [[ "$address" == 8* ]]; then

        #
        # Get UTXO information.
        #
        unspent=$(curl -sS "$API/api/unspent/$address")

        if [ -z "$unspent" ] || [ "$unspent" = "null" ]; then
            echo "ERROR: No API data found for $address" >&2
            source_index=$((source_index + 1))
            continue
        fi

        #
        # Get transaction ID.
        #
        txid=$(echo "$unspent" | jq -r '.[0].txid')

        if [ "$txid" = "null" ] || [ -z "$txid" ]; then
            echo "ERROR: No txid found for $address" >&2
            source_index=$((source_index + 1))
            continue
        fi

        #
        # Get full transaction.
        #
        tx=$(curl -sS "$API/api/tx/$txid")

        if [ -z "$tx" ] || [ "$tx" = "null" ]; then
            echo "ERROR: Could not retrieve transaction $txid" >&2
            source_index=$((source_index + 1))
            continue
        fi

        #
        # Find the nulldata output containing the redeem script.
        #
        redeem_data=$(echo "$tx" | jq -r '
            .vout[]
            | select(.scriptPubKey.type == "nulldata")
            | .scriptPubKey.asm
            | split(" ")
            | .[1]
        ' | head -n 1)

        if [ -z "$redeem_data" ] || [ "$redeem_data" = "null" ]; then
            echo "ERROR: No redeem script data found for $address (tx $txid)" >&2
            source_index=$((source_index + 1))
            continue
        fi


        #
        # Decode block height.
        #
        height_size_hex="${redeem_data:0:2}"
        height_size=$((16#$height_size_hex))

        if [ "$height_size" -le 0 ] || [ "$height_size" -gt 4 ]; then
            echo "ERROR: Invalid block height size for $address" >&2
            source_index=$((source_index + 1))
            continue
        fi

        height_hex="${redeem_data:2:$((height_size * 2))}"

        height_reversed=""

        for ((i=${#height_hex}; i>0; i-=2)); do
            height_reversed+="${height_hex:i-2:2}"
        done

        blockheight=$((16#$height_reversed))


        #
        # Locate compressed public key.
        #
        # <height> OP_CHECKLOCKTIMEVERIFY OP_DROP
        # <33-byte pubkey> OP_CHECKSIG
        #
        pubkey_offset=$((2 + height_size * 2 + 2 + 2))

        pubkey_size_hex="${redeem_data:$pubkey_offset:2}"
        pubkey_size=$((16#$pubkey_size_hex))

        if [ "$pubkey_size" -ne 33 ]; then
            echo "ERROR: Expected 33-byte compressed pubkey for $address, got $pubkey_size bytes" >&2
            source_index=$((source_index + 1))
            continue
        fi

        pubkey_start=$((pubkey_offset + 2))
        pubkey_length=$((pubkey_size * 2))

        pubkey="${redeem_data:$pubkey_start:$pubkey_length}"

        if [ "${#pubkey}" -ne 66 ]; then
            echo "ERROR: Invalid compressed pubkey for $address" >&2
            source_index=$((source_index + 1))
            continue
        fi


        #
        # Calculate HASH160(pubkey).
        #
        sha256_hex=$(echo -n "$pubkey" \
            | xxd -r -p \
            | openssl dgst -sha256 -binary \
            | xxd -p -c 256)

        hash160=$(echo -n "$sha256_hex" \
            | xxd -r -p \
            | openssl dgst -ripemd160 -binary \
            | xxd -p -c 256)

        if [ -z "$hash160" ]; then
            echo "ERROR: Could not calculate HASH160 for $address" >&2
            source_index=$((source_index + 1))
            continue
        fi


        #
        # Convert block height to unlock date.
        #
        days=$(( (blockheight - REFERENCE_BLOCK) / 144 ))

        unlock_date=$(date -u -d "$REFERENCE_DATE + $days days" +%Y-%m-%d)
        unlock_timestamp=$(date -u -d "$unlock_date" +%s)


        #
        # Generate legacy address from HASH160.
        #
        legacy_address=$(hash160_to_address "$hash160")

        if [ -z "$legacy_address" ]; then
            echo "ERROR: Could not generate legacy address for $address" >&2
            source_index=$((source_index + 1))
            continue
        fi


        #
        # Store RAW balance.
        #
        printf '%s\t%s\t%s\t%s\n' \
            "$hash160" \
            "$amount_raw" \
            "$unlock_timestamp" \
            "$legacy_address" >> "$TEMP"


        echo "  pubkey:           $pubkey"
        echo "  HASH160:          $hash160"
        echo "  legacy address:   $legacy_address"
        echo "  blockheight:      $blockheight"
        echo "  unlock date:      $unlock_date"
        echo "  unlock timestamp: $unlock_timestamp"


    else

        echo "ERROR: Unknown address type: $address" >&2
        source_index=$((source_index + 1))
        continue

    fi

    source_index=$((source_index + 1))

done < "$INPUT"


#
# Consolidation, sorting and output.
#
# IMPORTANT:
# The balance in TEMP is ALREADY raw 18-decimal units.
# Do NOT multiply it by 10^18 again.
#
python3 - "$TEMP" "$OUTPUT" "$READABLE" "$TODAY_TIMESTAMP" <<'PY'

import sys
from datetime import datetime, timezone

temp_file = sys.argv[1]
output_file = sys.argv[2]
readable_file = sys.argv[3]
today_timestamp = int(sys.argv[4])

SCALE = 10**18


def raw_to_8_decimals(raw):
    whole, remainder = divmod(raw, SCALE)

    fraction = f"{remainder:018d}"

    return f"{whole}.{fraction[:8]}"


def timestamp_to_date(timestamp):
    return datetime.fromtimestamp(
        timestamp,
        timezone.utc
    ).strftime("%Y-%m-%d")


#
# Read entries.
#
entries = []

with open(temp_file, "r") as f:

    for line_number, line in enumerate(f, 1):

        line = line.rstrip("\n")

        if not line:
            continue

        parts = line.split("\t")

        if len(parts) != 4:
            raise SystemExit(
                f"ERROR: Invalid temporary entry on line {line_number}"
            )

        hash160 = parts[0]

        #
        # This is ALREADY an 18-decimal raw integer.
        #
        raw_balance = int(parts[1])

        timestamp = int(parts[2])
        legacy_address = parts[3]

        entries.append({
            "hash160": hash160,
            "balance": raw_balance,
            "timestamp": timestamp,
            "address": legacy_address,
        })


#
# Consolidate unlocked entries.
#
# Same HASH160 + timestamp <= today:
#     combine into one entry.
#
# The resulting timestamp is TODAY.
#
unlocked = {}
future = []

for entry in entries:

    if entry["timestamp"] <= today_timestamp:

        hash160 = entry["hash160"]

        if hash160 not in unlocked:

            unlocked[hash160] = {
                "hash160": hash160,
                "balance": 0,
                "timestamp": today_timestamp,
                "address": entry["address"],
            }

        unlocked[hash160]["balance"] += entry["balance"]

    else:

        #
        # Future entries remain separate.
        #
        future.append(entry)


#
# Combine final entries.
#
final_entries = list(unlocked.values()) + future


#
# Sort by balance descending.
#
final_entries.sort(
    key=lambda entry: entry["balance"],
    reverse=True
)


#
# Generate both output files.
#
with open(output_file, "w") as data:
    with open(readable_file, "w") as readable:

        for index, entry in enumerate(final_entries, 1):

            hash160 = entry["hash160"]
            balance = entry["balance"]
            timestamp = entry["timestamp"]
            address = entry["address"]

            #
            # Machine-readable:
            #
            # index HASH160 raw_balance timestamp
            #
            data.write(
                f"{index} {hash160} {balance} {timestamp}\n"
            )

            #
            # Human-readable:
            #
            # index legacy_address balance date
            #
            balance_display = raw_to_8_decimals(balance)
            date_display = timestamp_to_date(timestamp)

            readable.write(
                f"{index} {address} "
                f"{balance_display} {date_display}\n"
            )


print()
print("Done.")
print(f"Machine-readable output: {output_file}")
print(f"Readable output:         {readable_file}")
print(f"Today's UTC date:        {timestamp_to_date(today_timestamp)}")
print(f"Final entries:           {len(final_entries)}")

PY
