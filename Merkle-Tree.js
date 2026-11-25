const fs = require("fs");
const { StandardMerkleTree } = require("@openzeppelin/merkle-tree");

// Read data
const lines = fs.readFileSync("data", "utf8")
    .split(/\r?\n/)
    .map(line => line.trim())
    .filter(line => line.length > 0);

// Parse:
// index pubkeyHash amountRaw unlockTimestamp
const entries = lines.map((line, lineNumber) => {
    const parts = line.split(/\s+/);

    if (parts.length !== 4) {
        throw new Error(
            `Invalid line ${lineNumber + 1}: expected "index pubkeyHash amountRaw unlockTimestamp"`
        );
    }

    const [index, pubkeyHash, amount, unlockTimestamp] = parts;

    if (!/^\d+$/.test(index)) {
        throw new Error(`Invalid index on line ${lineNumber + 1}`);
    }

    if (!/^[0-9a-fA-F]{40}$/.test(pubkeyHash)) {
        throw new Error(
            `Invalid pubkeyHash on line ${lineNumber + 1}: ${pubkeyHash}`
        );
    }

    if (!/^\d+$/.test(amount)) {
        throw new Error(
            `Invalid amount on line ${lineNumber + 1}: ${amount}`
        );
    }

    if (!/^\d+$/.test(unlockTimestamp)) {
        throw new Error(
            `Invalid unlockTimestamp on line ${lineNumber + 1}: ${unlockTimestamp}`
        );
    }

    return [
        index,
        "0x" + pubkeyHash.toLowerCase(),
        amount,
        unlockTimestamp
    ];
});

// Build the Merkle tree.
//
// Leaf = index + HASH160 + amount + unlockTimestamp
const tree = StandardMerkleTree.of(
    entries,
    ["uint256", "bytes20", "uint256", "uint256"]
);

// Display root
console.log("\nMerkle Root:");
console.log(tree.root);

// Generate claims.json WITHOUT Merkle proofs
const claims = {};

for (const [i, value] of tree.entries()) {
    const [index, pubkeyHash, amount, unlockTimestamp] = value;

    claims[index] = {
        pubkeyHash,
        amount,
        unlockTimestamp
    };
}

fs.writeFileSync(
    "claims.json",
    JSON.stringify(claims, null, 2)
);

console.log("\nGenerated claims.json");
console.log(`Entries: ${entries.length}`);
