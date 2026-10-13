// Theme Park World: vtables without RTTI. TP.exe has RTTI only for the std:: classes, so the New Game+
// skill's ApplyNames.java (which walks MSVC RTTI) finds no game classes. This script finds the game's
// vtables itself, creates the functions reached only through them (and other code pointers), and names
// them. Run it after ApplyNames.java and before ExportCaches.java:
//
//   analyzeHeadless re/ghidra TPW -process TP.exe -noanalysis
//       -scriptPath "<skill>/tools/ghidra;tools/ghidra"
//       -postScript ApplyNames.java re/names.tsv re/types.tsv
//       -postScript TPWVtables.java re/ghidra-export/vtables.tsv
//       -postScript ExportCaches.java re/ghidra-export
//
// 1. Vtables: an address in .rdata/.data that a MOV stores into memory (`mov [reg+d], imm32`, the
//    constructor pattern) and that starts a run of code pointers. The run ends at the first dword that
//    is not a code pointer or at the next vtable start. RTTI vtables (ApplyNames' job) are skipped.
// 2. Functions: every slot target that is not a function yet becomes one. A target inside an existing
//    function that follows padding or a ret/jmp splits that function (it ran on past its end).
//    Other code pointers (push/mov immediates, dwords in data) to an address that follows padding
//    become functions too (callbacks, keybind handlers, widget procs). Then auto-analysis runs on the
//    changes.
// 3. Names: the class of a vtable is the namespace of a "vftable" label at it (names.tsv:
//    "0x00705980  Widget::vftable"), else Class_<address>. An unnamed slot function goes into the class
//    of the smallest vtable that holds it at that slot (a base class has no more slots than the classes
//    derived from it) as vfXX (XX = slot offset) and becomes __thiscall; a function at different slots
//    of several vtables is a shared stub and is left alone. Unnamed functions that store a vtable into
//    `this` become Class::Class, or Class::~Class when they are a slot or are called from one. Names
//    from names.tsv are never changed. Re-runnable: ApplyNames resets these generated names first.
// 4. Types: one Class_vftable struct per vtable (a typed function pointer per slot) applied at the
//    vtable; a class struct that has a size (types.tsv) gets the vftable pointer at +0.
// Writes a vtables.tsv: class, vtable, slots, ctors/dtors, name@address per slot.
//@category NewGamePlus

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.*;

import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.lang.OperandType;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;

public class TPWVtables extends GhidraScript {

    private static final CategoryPath VT_CAT = new CategoryPath("/Game/vtables");
    private Memory mem;
    private final List<MemoryBlock> codeBlocks = new ArrayList<>();
    private final List<MemoryBlock> dataBlocks = new ArrayList<>();
    private int warnings = 0;
    private final Set<Long> sharedStubs = new HashSet<>();

    private static class VT {
        long addr;
        String cls;
        GhidraClass ns;
        List<Long> slots = new ArrayList<>();
        Set<String> ctors = new TreeSet<>();
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = args.length > 0 ? new File(args[0]) : null;
        mem = currentProgram.getMemory();
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized()) continue;
            if (b.isExecute()) codeBlocks.add(b);
            else if (!b.getName().equals(".rsrc")) dataBlocks.add(b);
        }
        int fnBefore = currentProgram.getFunctionManager().getFunctionCount();

        // ---- 1. vtable starts
        TreeSet<Long> starts = new TreeSet<>();
        List<long[]> codeImms = new ArrayList<>(); // {instruction address, value}
        for (MemoryBlock b : codeBlocks) {
            InstructionIterator it = currentProgram.getListing().getInstructions(new AddressSet(b.getStart(), b.getEnd()), true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Instruction ins = it.next();
                String m = ins.getMnemonicString();
                if (m.equals("MOV") && ins.getNumOperands() == 2) {
                    long v = immediate(ins, 1);
                    if (v < 0) continue;
                    int t0 = ins.getOperandType(0);
                    boolean memDst = (t0 & OperandType.DYNAMIC) != 0 || ((t0 & OperandType.ADDRESS) != 0 && (t0 & OperandType.REGISTER) == 0);
                    if (memDst && inData(v) && isCodePtr(u32(v))) starts.add(v);
                    if (inCode(v)) codeImms.add(new long[] {ins.getAddress().getOffset(), v});
                } else if (m.equals("PUSH") && ins.getNumOperands() == 1) {
                    long v = immediate(ins, 0);
                    if (v >= 0 && inCode(v)) codeImms.add(new long[] {ins.getAddress().getOffset(), v});
                }
            }
        }
        // labelled vftables from names.tsv count as starts too
        for (Symbol s : currentProgram.getSymbolTable().getSymbols("vftable")) {
            long a = s.getAddress().getOffset();
            if (inData(a) && isCodePtr(u32(a))) starts.add(a);
        }
        List<VT> vts = new ArrayList<>();
        int rttiSkipped = 0;
        for (long s : starts) {
            if (isRttiVtable(s)) { rttiSkipped++; continue; }
            VT v = new VT();
            v.addr = s;
            for (long a = s; a < s + 4 * 1024; a += 4) {
                if (a != s && starts.contains(a)) break;
                if (!mem.contains(toAddr(a))) break;
                long t = u32(a);
                if (!isCodePtr(t)) break;
                v.slots.add(t);
            }
            if (!v.slots.isEmpty()) vts.add(v);
        }
        int slotCount = 0;
        for (VT v : vts) slotCount += v.slots.size();
        println("vtables: " + vts.size() + " (" + slotCount + " slots; " + rttiSkipped + " RTTI vtables left to ApplyNames)");

        // ---- 2. functions
        int created = 0, split = 0, failed = 0;
        Set<Long> slotTargets = new TreeSet<>();
        for (VT v : vts) slotTargets.addAll(v.slots);
        for (long t : slotTargets) {
            int r = ensureFunction(t, true);
            if (r == 1) created++; else if (r == 2) split++; else if (r < 0) failed++;
        }
        println("vtable slot functions: " + slotTargets.size() + " distinct, " + created + " created, " + split
                + " split out of a longer function, " + failed + " not possible");
        int cCreated = 0, cSplit = 0, cFailed = 0;
        Set<Long> others = new TreeSet<>();
        for (long[] ci : codeImms) others.add(ci[1]);
        for (MemoryBlock b : dataBlocks) {
            long s = b.getStart().getOffset(), e = b.getEnd().getOffset();
            for (long a = (s + 3) & ~3L; a + 4 <= e + 1; a += 4) {
                long t = u32(a);
                if (inCode(t)) others.add(t);
            }
        }
        others.removeAll(slotTargets);
        for (long t : others) {
            if (getFunctionAt(toAddr(t)) != null) continue;
            if (!followsPadding(t)) continue;
            int r = ensureFunction(t, false);
            if (r == 1) cCreated++; else if (r == 2) cSplit++; else if (r < 0) cFailed++;
        }
        println("other code pointers after padding: " + cCreated + " functions created, " + cSplit + " split, " + cFailed + " not possible");
        if (created + split + cCreated + cSplit > 0) {
            println("analysing the changes...");
            analyzeChanges(currentProgram);
        }
        int cut = 0;
        for (VT v : vts) {
            for (int i = 0; i < v.slots.size(); i++) {
                if (getFunctionAt(toAddr(v.slots.get(i))) == null) {
                    cut += v.slots.size() - i;
                    v.slots = new ArrayList<>(v.slots.subList(0, i));
                    break;
                }
            }
        }
        vts.removeIf(v -> v.slots.isEmpty());
        println("vtables after dropping trailing non-function slots: " + vts.size() + " (" + cut + " slots dropped)");
        int fnAfter = currentProgram.getFunctionManager().getFunctionCount();
        println("functions: " + fnBefore + " -> " + fnAfter);

        // ---- 3a. static initialisers: the longest run of code pointers in data is the CRT's __xc_a..__xc_z
        // table (TP.exe: 7225 entries at 0x00727004). Unnamed targets become StaticInit_<addr>; the functions
        // they register with atexit (push <code>; call atexit) become StaticDtor_<addr>.
        int sInit = 0, sDtor = 0;
        long[] xc = longestCodePtrRun();
        if (xc != null && xc[2] >= 256) {
            label(toAddr(xc[0]), "g_StaticCtorTable", currentProgram.getGlobalNamespace());
            Map<Long, Integer> callAfterPush = new HashMap<>();
            List<Function> inits = new ArrayList<>();
            for (long a = xc[0]; a < xc[1]; a += 4) {
                Function f = getFunctionAt(toAddr(u32(a)));
                if (f == null) continue;
                inits.add(f);
                if (isDefaultName(f) && f.getParentNamespace().isGlobal()) {
                    f.setName(String.format("StaticInit_%08x", f.getEntryPoint().getOffset()), SourceType.ANALYSIS);
                    sInit++;
                }
                for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                    Instruction nx = ins.getNext();
                    if (!ins.getMnemonicString().equals("PUSH") || nx == null || !nx.getMnemonicString().equals("CALL")) continue;
                    long v = immediate(ins, 0);
                    if (v < 0 || !inCode(v)) continue;
                    Address[] fl = nx.getFlows();
                    if (fl.length == 1) callAfterPush.merge(fl[0].getOffset(), 1, Integer::sum);
                }
            }
            long atexit = -1;
            int best = 0;
            for (Map.Entry<Long, Integer> e : callAfterPush.entrySet()) if (e.getValue() > best) { best = e.getValue(); atexit = e.getKey(); }
            if (atexit >= 0 && best >= 100) {
                Function ae = getFunctionAt(toAddr(atexit));
                if (ae != null && isDefaultName(ae)) ae.setName("CRT_atexit", SourceType.ANALYSIS);
                for (Function f : inits) {
                    for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                        Instruction nx = ins.getNext();
                        if (!ins.getMnemonicString().equals("PUSH") || nx == null || !nx.getMnemonicString().equals("CALL")) continue;
                        Address[] fl = nx.getFlows();
                        if (fl.length != 1 || fl[0].getOffset() != atexit) continue;
                        long v = immediate(ins, 0);
                        Function d = v >= 0 && inCode(v) ? getFunctionAt(toAddr(v)) : null;
                        if (d != null && isDefaultName(d) && d.getParentNamespace().isGlobal()) {
                            d.setName(String.format("StaticDtor_%08x", v), SourceType.ANALYSIS);
                            sDtor++;
                        }
                    }
                }
            }
            println("static initialisers: table " + Long.toHexString(xc[0]) + ".." + Long.toHexString(xc[1]) + " (" + xc[2]
                    + " entries), " + sInit + " StaticInit_, " + sDtor + " StaticDtor_ named, atexit " + Long.toHexString(atexit));
        }

        // ---- 3. names
        for (VT v : vts) {
            Namespace ns = null;
            for (Symbol s : currentProgram.getSymbolTable().getSymbols(toAddr(v.addr))) {
                if (s.getName().startsWith("vftable") && !s.getParentNamespace().isGlobal()) ns = s.getParentNamespace();
            }
            if (ns == null) {
                ns = classNamespace(String.format("Class_%08x", v.addr));
                label(toAddr(v.addr), "vftable", ns);
            }
            if (!(ns instanceof GhidraClass)) ns = NamespaceUtils.convertNamespaceToClass(ns);
            v.ns = (GhidraClass) ns;
            v.cls = ns.getName(true);
        }
        Map<Long, List<long[]>> holders = new HashMap<>(); // fn -> {vt index, slot}
        for (int i = 0; i < vts.size(); i++) {
            VT v = vts.get(i);
            for (int s = 0; s < v.slots.size(); s++) holders.computeIfAbsent(v.slots.get(s), k -> new ArrayList<>()).add(new long[] {i, s});
        }
        int vf = 0, stubs = 0, thiscall = 0;
        for (Map.Entry<Long, List<long[]>> e : holders.entrySet()) {
            Function f = getFunctionAt(toAddr(e.getKey()));
            if (f == null || f.isThunk() || f.isExternal()) continue;
            Set<Long> slotsSeen = new HashSet<>();
            for (long[] h : e.getValue()) slotsSeen.add(h[1]);
            if (slotsSeen.size() > 1) { stubs++; sharedStubs.add(e.getKey()); continue; }
            VT owner = null;
            for (long[] h : e.getValue()) {
                VT v = vts.get((int) h[0]);
                if (owner == null || v.slots.size() < owner.slots.size()
                        || (v.slots.size() == owner.slots.size() && v.addr < owner.addr)) owner = v;
            }
            int slot = (int) e.getValue().get(0)[1];
            if (isDefaultName(f)) {
                try {
                    f.setParentNamespace(owner.ns);
                    f.setName(String.format("vf%02x", slot * 4), SourceType.ANALYSIS);
                    vf++;
                } catch (Exception ex) { warn(f.getEntryPoint() + ": " + ex.getMessage()); }
                if (toThiscall(f, true)) thiscall++;
            } else if (f.getParentNamespace().isGlobal() || f.getParentNamespace() instanceof GhidraClass) {
                if (toThiscall(f, false)) thiscall++; // named in names.tsv: only fix Ghidra's __fastcall guess
            }
        }
        // constructors / destructors
        Function opDelete = null;
        for (Symbol sy : currentProgram.getSymbolTable().getSymbols("operator_delete")) {
            Function od = getFunctionAt(sy.getAddress());
            if (od != null) opDelete = od;
        }
        Set<Long> slotFns = holders.keySet();
        Map<Function, List<long[]>> writers = new HashMap<>(); // fn -> {store address, vt index}
        for (int i = 0; i < vts.size(); i++) {
            VT v = vts.get(i);
            for (Reference r : getReferencesTo(toAddr(v.addr))) {
                Function f = getFunctionContaining(r.getFromAddress());
                if (f == null) continue;
                Instruction ins = getInstructionAt(r.getFromAddress());
                if (ins == null || !ins.getMnemonicString().equals("MOV") || !storesToThis(ins)) continue;
                writers.computeIfAbsent(f, k -> new ArrayList<>()).add(new long[] {ins.getAddress().getOffset(), i});
            }
        }
        int ctors = 0, dtors = 0;
        for (Map.Entry<Function, List<long[]>> e : writers.entrySet()) {
            Function f = e.getKey();
            // a destructor is a vtable slot itself (scalar deleting destructor) or is called from a slot
            // that also calls operator delete
            boolean dtor = slotFns.contains(f.getEntryPoint().getOffset());
            if (!dtor && opDelete != null) {
                for (Function caller : f.getCallingFunctions(monitor)) {
                    if (slotFns.contains(caller.getEntryPoint().getOffset()) && caller.getCalledFunctions(monitor).contains(opDelete)) {
                        dtor = true;
                        break;
                    }
                }
            }
            List<long[]> st = e.getValue();
            st.sort(Comparator.comparingLong(x -> x[0]));
            VT v = vts.get((int) (dtor ? st.get(0)[1] : st.get(st.size() - 1)[1])); // derived: last store in a ctor, first in a dtor
            if (!isDefaultName(f)) {
                v.ctors.add((dtor ? "dtor:" : "ctor:") + f.getName(true) + "@" + Long.toHexString(f.getEntryPoint().getOffset()));
                continue;
            }
            String simple = v.ns.getName();
            try {
                f.setParentNamespace(v.ns);
                f.setName(dtor ? "~" + simple : simple, SourceType.ANALYSIS);
                if (dtor) dtors++; else ctors++;
                toThiscall(f, true);
                v.ctors.add((dtor ? "dtor:" : "ctor:") + f.getName(true) + "@" + Long.toHexString(f.getEntryPoint().getOffset()));
            } catch (Exception ex) { warn(f.getEntryPoint() + ": " + ex.getMessage()); }
        }
        println("names: " + vf + " Class::vfXX, " + ctors + " ctors, " + dtors + " dtors, " + stubs + " shared stubs left alone, "
                + thiscall + " made __thiscall");

        // ---- 4. types
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        int vtStructs = 0, classPtrs = 0;
        Set<String> classesDone = new HashSet<>();
        for (VT v : vts) {
            try {
                String n = v.cls.replace("::", "_").replaceAll("[^A-Za-z0-9_]", "_") + "_vftable";
                if (!classesDone.add(n)) n = n + "_" + Long.toHexString(v.addr);
                Structure s = new StructureDataType(VT_CAT, n, 0, dtm);
                Set<String> used = new HashSet<>();
                for (int i = 0; i < v.slots.size(); i++) {
                    Function f = getFunctionAt(toAddr(v.slots.get(i)));
                    // a stub shared by unrelated slots (purecall, empty method) would name the field wrongly
                    String fname = f == null || sharedStubs.contains(v.slots.get(i)) ? String.format("vf%02x", i * 4)
                            : f.getName().replaceAll("[^A-Za-z0-9_]", "_");
                    if (!used.add(fname)) fname = fname + String.format("_%02x", i * 4);
                    DataType dt = PointerDataType.dataType;
                    if (f != null) {
                        String fd = f.getName(true).replace("::", "__").replaceAll("[^A-Za-z0-9_]", "_");
                        FunctionDefinitionDataType fdt = new FunctionDefinitionDataType(VT_CAT.extend("functions"), fd, f.getSignature(true), dtm);
                        dt = new PointerDataType(dtm.resolve(fdt, DataTypeConflictHandler.REPLACE_HANDLER), 4, dtm);
                    }
                    s.add(dt, 4, fname, f == null ? null : f.getName(true) + " @ " + f.getEntryPoint());
                }
                DataType r = dtm.resolve(s, DataTypeConflictHandler.REPLACE_HANDLER);
                DataUtilities.createData(currentProgram, toAddr(v.addr), r, -1, false, DataUtilities.ClearDataMode.CLEAR_ALL_CONFLICT_DATA);
                vtStructs++;
                // vftable pointer at +0 of a class struct with a known size
                Structure cs = VariableUtilities.findOrCreateClassStruct(v.ns, dtm);
                if (cs != null && !cs.isZeroLength() && cs.getLength() >= 4) {
                    DataTypeComponent c0 = cs.getComponentAt(0);
                    if (c0 == null || c0.getDataType() == DataType.DEFAULT || "vftable".equals(c0.getFieldName())) {
                        cs.replaceAtOffset(0, new PointerDataType(r, 4, dtm), 4, "vftable", null);
                        classPtrs++;
                    }
                }
            } catch (Exception ex) { warn("vtable " + Long.toHexString(v.addr) + ": " + ex.getMessage()); }
        }
        println("vtable structs: " + vtStructs + ", class structs given a vftable pointer: " + classPtrs);

        // ---- report
        if (out != null) {
            out.getParentFile().mkdirs();
            try (PrintWriter w = new PrintWriter(out, StandardCharsets.UTF_8)) {
                w.println("class\tvtable\tslots\tctors_dtors\tvirtual_functions");
                for (VT v : vts) {
                    StringBuilder sb = new StringBuilder();
                    for (long t : v.slots) {
                        Function f = getFunctionAt(toAddr(t));
                        if (sb.length() > 0) sb.append(' ');
                        sb.append(f == null ? "?" : f.getName(true)).append('@').append(Long.toHexString(t));
                    }
                    w.printf("%s\t%08x\t%d\t%s\t%s%n", v.cls, v.addr, v.slots.size(), String.join(" ", v.ctors), sb);
                }
            }
            println("wrote " + out);
        }
        println("warnings: " + warnings);
    }

    // =========================================================================================

    /** {start, end, count} of the longest run of 4-aligned code pointers in a data block. */
    private long[] longestCodePtrRun() {
        long[] best = null;
        for (MemoryBlock b : dataBlocks) {
            long s = (b.getStart().getOffset() + 3) & ~3L, e = b.getEnd().getOffset();
            long runStart = -1;
            for (long a = s; a + 4 <= e + 1; a += 4) {
                boolean c = inCode(u32(a));
                if (c && runStart < 0) runStart = a;
                if ((!c || a + 8 > e + 1) && runStart >= 0) {
                    long end = c ? a + 4 : a;
                    long n = (end - runStart) / 4;
                    if (best == null || n > best[2]) best = new long[] {runStart, end, n};
                    runStart = -1;
                }
            }
        }
        return best;
    }

    /** 1 created, 2 split from a containing function, 0 exists already, -1 failed. */
    private int ensureFunction(long t, boolean isSlot) {
        Address a = toAddr(t);
        try {
            if (getFunctionAt(a) != null) return 0;
            Function host = getFunctionContaining(a);
            if (host != null) {
                // a vtable slot is an entry for sure: split after padding or a ret/jmp. Other pointers can be
                // __except handlers or case labels inside a function: split only at aligned nop/int3 padding.
                boolean ok = isSlot ? followsPadding(t) || followsTerminator(a) : (t & 0xf) == 0 && followsNopPadding(t);
                if (!ok) return -1;
                // cut the host's body from a to the end of the address range (chunk) holding a
                AddressRange chunk = host.getBody().getRangeContaining(a);
                if (chunk == null || chunk.contains(host.getEntryPoint()) && host.getEntryPoint().compareTo(a) >= 0) return -1;
                AddressSet nb = new AddressSet(host.getBody());
                nb.deleteRange(a, chunk.getMaxAddress());
                if (nb.isEmpty() || !nb.contains(host.getEntryPoint())) return -1;
                host.setBody(nb);
                Function f = createFunction(a, null);
                if (f == null) return -1;
                CreateFunctionCmd.fixupFunctionBody(currentProgram, host, monitor);
                println("split: " + a + " out of " + host.getName(true) + " @ " + host.getEntryPoint());
                return 2;
            }
            if (getInstructionAt(a) == null) {
                Data d = getDataContaining(a);
                if (d != null && d.isDefined()) return -1;
                if (!disassemble(a)) return -1;
            }
            if (getInstructionAt(a) == null) return -1;
            return createFunction(a, null) != null ? 1 : -1;
        } catch (Exception ex) {
            warn(a + ": " + ex.getMessage());
            return -1;
        }
    }

    /** The bytes before t are alignment padding (nop/int3) or a ret: t starts a new function. */
    private boolean followsPadding(long t) {
        try {
            int b1 = mem.getByte(toAddr(t - 1)) & 0xff;
            if (b1 == 0x90 || b1 == 0xcc || b1 == 0xc3) return true;
            int b3 = mem.getByte(toAddr(t - 3)) & 0xff;
            return b3 == 0xc2 && (mem.getByte(toAddr(t - 1)) & 0xff) == 0;
        } catch (Exception ex) {
            return false;
        }
    }

    private boolean followsNopPadding(long t) {
        try {
            int b1 = mem.getByte(toAddr(t - 1)) & 0xff;
            return b1 == 0x90 || b1 == 0xcc;
        } catch (Exception ex) {
            return false;
        }
    }

    /** The instruction before a does not fall through into it (ret / jmp). */
    private boolean followsTerminator(Address a) {
        Instruction prev = getInstructionContaining(a.subtract(1));
        if (prev == null) return false;
        String m = prev.getMnemonicString();
        return m.equals("RET") || m.equals("JMP");
    }

    private boolean isRttiVtable(long vt) {
        try {
            long col = u32(vt - 4);
            if (!inData(col)) return false;
            for (Symbol s : currentProgram.getSymbolTable().getSymbols(toAddr(col))) {
                if (s.getName().startsWith("RTTI_Complete_Object_Locator")) return true;
            }
        } catch (Exception ignored) {
        }
        return false;
    }

    private long immediate(Instruction ins, int op) {
        for (Object o : ins.getOpObjects(op)) {
            if (o instanceof Scalar) return ((Scalar) o).getUnsignedValue();
            if (o instanceof Address && (ins.getOperandType(op) & OperandType.DYNAMIC) == 0
                    && (ins.getOperandType(op) & OperandType.REGISTER) == 0) {
                // an immediate Ghidra shows as an address; a memory operand [addr] has ADDRESS|DATA too, so
                // accept it only when the operand is not a memory reference
                if ((ins.getOperandType(op) & OperandType.INDIRECT) == 0 && ins.getDefaultOperandRepresentation(op).indexOf('[') < 0)
                    return ((Address) o).getOffset();
            }
        }
        return -1;
    }

    private boolean inCode(long v) {
        if (v <= 0 || v > 0xffffffffL) return false;
        Address a = toAddr(v);
        for (MemoryBlock b : codeBlocks) if (b.contains(a)) return true;
        return false;
    }

    private boolean inData(long v) {
        if (v <= 0 || v > 0xffffffffL) return false;
        Address a = toAddr(v);
        for (MemoryBlock b : dataBlocks) if (b.contains(a)) return true;
        return false;
    }

    private boolean isCodePtr(long v) {
        return inCode(v);
    }

    private long u32(long a) {
        try {
            return mem.getInt(toAddr(a)) & 0xffffffffL;
        } catch (Exception ex) {
            return -1;
        }
    }

    private boolean isDefaultName(Function f) {
        SourceType s = f.getSymbol().getSource();
        if (f.isThunk()) return false;
        return s == SourceType.DEFAULT || (s == SourceType.ANALYSIS && (f.getName().startsWith("vf") || f.getName().startsWith("FUN_")
                || f.getName().equals(f.getParentNamespace().getName()) || f.getName().startsWith("~")));
    }

    private Namespace classNamespace(String name) throws Exception {
        Namespace g = currentProgram.getGlobalNamespace();
        Namespace ns = currentProgram.getSymbolTable().getNamespace(name, g);
        if (ns == null) ns = currentProgram.getSymbolTable().createClass(g, name, SourceType.ANALYSIS);
        if (!(ns instanceof GhidraClass)) ns = NamespaceUtils.convertNamespaceToClass(ns);
        return ns;
    }

    private void label(Address a, String name, Namespace ns) throws Exception {
        for (Symbol s : currentProgram.getSymbolTable().getSymbols(a)) {
            if (s.getName().equals(name) && s.getParentNamespace().equals(ns)) return;
        }
        currentProgram.getSymbolTable().createLabel(a, name, ns, SourceType.ANALYSIS);
    }

    // "mov dword ptr [reg], vftable" with no displacement and reg not ESP/EBP, reg still holding the
    // incoming ECX: the object at this (from ApplyNames.java).
    private boolean storesToThis(Instruction ins) {
        Object[] objs = ins.getOpObjects(0);
        if (objs.length != 1 || !(objs[0] instanceof Register)) return false;
        if ((ins.getOperandType(0) & OperandType.DYNAMIC) == 0) return false;
        String r = ((Register) objs[0]).getName();
        if (r.equals("ESP") || r.equals("EBP")) return false;
        return holdsEntryEcx(ins, r);
    }

    private boolean holdsEntryEcx(Instruction store, String r) {
        Function f = getFunctionContaining(store.getAddress());
        if (f == null) return false;
        Set<String> held = new HashSet<>(Set.of("ECX"));
        Instruction ins = getInstructionAt(f.getEntryPoint());
        int guard = 0;
        while (ins != null && ins.getAddress().compareTo(store.getAddress()) < 0 && guard++ < 400) {
            String m = ins.getMnemonicString();
            if (m.equals("CALL")) {
                held.remove("EAX"); held.remove("ECX"); held.remove("EDX");
            } else if (ins.getNumOperands() >= 1) {
                Object[] dst = ins.getOpObjects(0);
                boolean regDst = dst.length == 1 && dst[0] instanceof Register && (ins.getOperandType(0) & OperandType.DYNAMIC) == 0;
                if (regDst && !m.equals("PUSH") && !m.equals("CMP") && !m.equals("TEST")) {
                    String d = ((Register) dst[0]).getBaseRegister().getName();
                    boolean copy = false;
                    if (m.equals("MOV") && ins.getNumOperands() == 2) {
                        Object[] src = ins.getOpObjects(1);
                        if (src.length == 1 && src[0] instanceof Register && (ins.getOperandType(1) & OperandType.DYNAMIC) == 0) {
                            copy = held.contains(((Register) src[0]).getBaseRegister().getName());
                        }
                    }
                    if (copy) held.add(d); else held.remove(d);
                }
            }
            ins = ins.getNext();
        }
        return held.contains(r);
    }

    // MSVC methods are __thiscall. Ghidra guesses __fastcall for a method without stack arguments:
    // convert that (the ECX parameter becomes `this`). force: also convert other conventions.
    private boolean toThiscall(Function f, boolean force) {
        try {
            String cc = f.getCallingConventionName();
            if (f.hasVarArgs() || "__thiscall".equals(cc)) return false;
            if (f.getSignatureSource() == SourceType.USER_DEFINED) return false;
            if ("__fastcall".equals(cc)) {
                List<Variable> keep = new ArrayList<>();
                for (Parameter p : f.getParameters()) {
                    if (p.isRegisterVariable()) {
                        if (p.getRegister().getName().equals("EDX")) return false; // a real fastcall
                        continue;
                    }
                    keep.add(new ParameterImpl(p.getName(), p.getDataType(), currentProgram));
                }
                f.updateFunction("__thiscall", f.getReturn(), keep, Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS,
                        true, f.getSignatureSource());
                return true;
            } else if (force) {
                f.setCallingConvention("__thiscall");
                return true;
            }
        } catch (Exception ex) {
            warn(f.getName(true) + ": thiscall: " + ex.getMessage());
        }
        return false;
    }

    private void warn(String msg) {
        warnings++;
        if (warnings <= 200) println("WARN " + msg);
    }
}
