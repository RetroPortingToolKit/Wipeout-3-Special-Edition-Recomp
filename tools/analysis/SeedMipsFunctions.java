// Seed a raw MIPS image with explicitly supplied, untrusted discovery roots.
//@category StaticRecomp

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.TreeSet;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;

public class SeedMipsFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length != 1) {
            throw new IllegalArgumentException("expected one roots-file argument");
        }
        Path rootsPath = Path.of(arguments[0]);
        List<String> lines = Files.readAllLines(rootsPath, StandardCharsets.UTF_8);
        TreeSet<Long> roots = new TreeSet<>();
        long previous = -1;
        for (int index = 0; index < lines.size(); ++index) {
            String line = lines.get(index).trim();
            if (!line.matches("0x[0-9A-Fa-f]{8}")) {
                throw new IllegalArgumentException(
                    "invalid root at line " + (index + 1));
            }
            long value = Long.parseUnsignedLong(line.substring(2), 16);
            if ((value & 3) != 0) {
                throw new IllegalArgumentException(
                    "unaligned root at line " + (index + 1));
            }
            if (previous >= 0 && Long.compareUnsigned(value, previous) <= 0) {
                throw new IllegalArgumentException(
                    "roots must be strictly increasing and unique");
            }
            roots.add(value);
            previous = value;
        }
        if (roots.isEmpty()) {
            throw new IllegalArgumentException("roots file is empty");
        }

        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        int disassembled = 0;
        int functions = 0;
        int rejected = 0;
        for (long value : roots) {
            monitor.checkCancelled();
            Address address = space.getAddress(value);
            if (!currentProgram.getMemory().contains(address)) {
                ++rejected;
                continue;
            }
            if (getInstructionAt(address) == null && disassemble(address)) {
                ++disassembled;
            }
            Function function = getFunctionAt(address);
            if (function == null && getInstructionAt(address) != null) {
                function = createFunction(address, null);
                if (function != null) {
                    ++functions;
                }
            }
            currentProgram.getSymbolTable().addExternalEntryPoint(address);
        }
        println("seed roots=" + roots.size() + " disassembled=" + disassembled +
            " functions=" + functions + " rejected=" + rejected);
    }
}
