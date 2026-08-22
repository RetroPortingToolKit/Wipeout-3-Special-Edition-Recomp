// Export deterministic, hashable function-analysis hints from a Ghidra project.
//@category StaticRecomp

import java.io.BufferedWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressRangeIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.InstructionIterator;

public class ExportFunctionInventory extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length != 1) {
            throw new IllegalArgumentException("expected one output-file argument");
        }
        Path output = Path.of(arguments[0]);
        if (Files.exists(output)) {
            throw new IllegalArgumentException("refusing to overwrite output");
        }
        Path parent = output.toAbsolutePath().getParent();
        if (parent != null) {
            Files.createDirectories(parent);
        }

        JsonArray records = new JsonArray();
        long totalInstructions = 0;
        FunctionIterator iterator = currentProgram.getFunctionManager().getFunctions(true);
        while (iterator.hasNext()) {
            monitor.checkCancelled();
            Function function = iterator.next();
            JsonObject record = new JsonObject();
            record.addProperty("entry", function.getEntryPoint().getOffset());
            record.addProperty("name", function.getName());
            record.addProperty("is_thunk", function.isThunk());
            record.addProperty("body_bytes", function.getBody().getNumAddresses());

            JsonArray ranges = new JsonArray();
            AddressRangeIterator rangeIterator = function.getBody().getAddressRanges(true);
            while (rangeIterator.hasNext()) {
                AddressRange range = rangeIterator.next();
                JsonObject rangeRecord = new JsonObject();
                rangeRecord.addProperty("start", range.getMinAddress().getOffset());
                rangeRecord.addProperty(
                    "end_exclusive", range.getMaxAddress().getOffset() + 1);
                ranges.add(rangeRecord);
            }
            record.add("body_ranges", ranges);

            long instructionCount = 0;
            InstructionIterator instructions =
                currentProgram.getListing().getInstructions(function.getBody(), true);
            while (instructions.hasNext()) {
                instructions.next();
                ++instructionCount;
            }
            record.addProperty("instruction_count", instructionCount);
            totalInstructions += instructionCount;
            records.add(record);
        }

        JsonObject report = new JsonObject();
        report.addProperty("schema", "psxrecomp.ghidra-function-hints.v1");
        report.addProperty(
            "classification", "untrusted analysis hints; not executable coverage");
        report.addProperty("language", currentProgram.getLanguageID().toString());
        report.addProperty("function_count", records.size());
        report.addProperty("instruction_count", totalInstructions);
        report.add("functions", records);

        Gson gson = new GsonBuilder().setPrettyPrinting().disableHtmlEscaping().create();
        try (BufferedWriter writer = Files.newBufferedWriter(
                output,
                StandardCharsets.UTF_8,
                StandardOpenOption.CREATE_NEW,
                StandardOpenOption.WRITE)) {
            gson.toJson(report, writer);
            writer.write('\n');
        }
        println("exported functions=" + records.size() + " instructions=" +
            totalInstructions);
    }
}
