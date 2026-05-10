/*
 * One-shot QR-code fixture generator using BoofCV's QrCodeEncoder +
 * QrCodeGeneratorImage. Emits PNGs + JSON ground-truth files into the
 * output directory. Run via:
 *
 *   ./gradlew run --args="<outDir>"
 *
 * with `<outDir>` defaulting to ../../tests/fixtures/qr if not given.
 *
 * Each fixture is one row in `MATRIX[]`: (name, version, errorLevel,
 * maskPattern, payload, mode). The generator's QrCode result includes
 * the 4-corner image-pixel coordinates of each finder pattern, the
 * resolved mask number (which the encoder auto-selects when null is
 * passed via setMask), and the actual payload bytes for byte-mode
 * inputs. Output JSON keys mirror the format the C++ tests load.
 *
 * The mask number IS surfaced in the JSON as `maskBits` (000-111) so
 * the parametric `full_simple` parity test (codex review fix-up #8)
 * can assert encoded mask == decoded mask.
 */
package qrboofcv;

import boofcv.alg.fiducial.qrcode.QrCode;
import boofcv.alg.fiducial.qrcode.QrCodeEncoder;
import boofcv.alg.fiducial.qrcode.QrCodeGeneratorImage;
import boofcv.alg.fiducial.qrcode.QrCodeMaskPattern;
import boofcv.io.image.UtilImageIO;
import boofcv.struct.image.GrayU8;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import georegression.struct.point.Point2D_F64;
import georegression.struct.shapes.Polygon2D_F64;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class GenerateFixtures {

    /** One fixture spec. mask=null => let the encoder auto-select. */
    static record Fixture(
            String name,
            int version,
            QrCode.ErrorLevel error,
            QrCodeMaskPattern mask,
            String payload,
            Mode mode) {
        enum Mode { NUMERIC, ALPHANUMERIC, BYTE, KANJI, MIXED }
    }

    /** Returns the bit-pattern integer for the given mask (000..111). */
    static int maskBits(QrCodeMaskPattern m) {
        if (m == QrCodeMaskPattern.M000) return 0b000;
        if (m == QrCodeMaskPattern.M001) return 0b001;
        if (m == QrCodeMaskPattern.M010) return 0b010;
        if (m == QrCodeMaskPattern.M011) return 0b011;
        if (m == QrCodeMaskPattern.M100) return 0b100;
        if (m == QrCodeMaskPattern.M101) return 0b101;
        if (m == QrCodeMaskPattern.M110) return 0b110;
        if (m == QrCodeMaskPattern.M111) return 0b111;
        throw new IllegalArgumentException("Unknown mask: " + m);
    }

    static String errorName(QrCode.ErrorLevel e) {
        return e.name();  // L, M, Q, H
    }

    static QrCodeEncoder seedEncoder(Fixture fx) {
        QrCodeEncoder enc = new QrCodeEncoder().setVersion(fx.version)
                                                .setError(fx.error);
        if (fx.mask != null) enc.setMask(fx.mask);
        switch (fx.mode) {
            case NUMERIC -> enc.addNumeric(fx.payload);
            case ALPHANUMERIC -> enc.addAlphanumeric(fx.payload);
            case BYTE -> enc.addBytes(fx.payload);
            case KANJI -> enc.addKanji(fx.payload);
            case MIXED -> {
                // Default: split heuristically into kanji + alphanum.
                // Test fixtures pass MIXED with payload "kanji-part|alpha-part".
                String[] parts = fx.payload.split("\\|", 2);
                if (parts.length == 2) {
                    enc.addKanji(parts[0]);
                    enc.addAlphanumeric(parts[1]);
                } else {
                    enc.addAlphanumeric(fx.payload);
                }
            }
        }
        return enc;
    }

    static ArrayNode toJsonPolygon(Polygon2D_F64 p, ObjectMapper mapper) {
        ArrayNode out = mapper.createArrayNode();
        for (int i = 0; i < p.size(); i++) {
            Point2D_F64 c = p.get(i);
            ArrayNode pt = mapper.createArrayNode();
            pt.add(c.x);
            pt.add(c.y);
            out.add(pt);
        }
        return out;
    }

    static String fmtPolygon(Polygon2D_F64 p) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < p.size(); i++) {
            if (i > 0) sb.append(' ');
            Point2D_F64 c = p.get(i);
            sb.append(c.x).append(',').append(c.y);
        }
        return sb.toString();
    }

    static void emit(Fixture fx, File outDir) throws IOException {
        QrCode qr = seedEncoder(fx).fixate();

        QrCodeGeneratorImage gen = new QrCodeGeneratorImage(4);
        // Default borderModule=2 (Java's default; matches JUnit setup).
        gen.render(qr);
        GrayU8 gray = gen.getGray();

        // Save PNG.
        UtilImageIO.saveImage(gray, new File(outDir, fx.name + ".png").getAbsolutePath());

        int maskValue = maskBits(qr.mask);
        int numModules = QrCode.totalModules(qr.version);

        // Emit JSON ground-truth.
        ObjectMapper mapper = new ObjectMapper();
        ObjectNode root = mapper.createObjectNode();
        root.put("name", fx.name);
        root.put("version", qr.version);
        root.put("error", errorName(qr.error));
        root.put("mode", fx.mode.name());
        root.put("message", qr.message);  // qr.message is set by the encoder for known modes
        root.put("payload", fx.payload);
        root.put("module_pixels", 4);
        root.put("border_modules", gen.getBorderModule());
        root.put("image_size", gray.width);
        root.put("num_modules", numModules);
        root.put("maskBits", maskValue);
        root.put("maskName", "M" + Integer.toBinaryString(0b1000 | maskValue).substring(1));
        root.set("ppCorner", toJsonPolygon(qr.ppCorner, mapper));
        root.set("ppRight", toJsonPolygon(qr.ppRight, mapper));
        root.set("ppDown", toJsonPolygon(qr.ppDown, mapper));
        mapper.writerWithDefaultPrettyPrinter().writeValue(
                new File(outDir, fx.name + ".json"), root);

        // Emit flat key=value file the C++ tests parse without a JSON dep.
        StringBuilder txt = new StringBuilder();
        txt.append("name = ").append(fx.name).append('\n');
        txt.append("version = ").append(qr.version).append('\n');
        txt.append("error = ").append(errorName(qr.error)).append('\n');
        txt.append("mode = ").append(fx.mode.name()).append('\n');
        // `message` may have non-ASCII chars — write as UTF-8 bytes; the
        // simple key=value parser splits on '=' and reads the rest of
        // the line raw, so newlines in the payload would break it. Our
        // fixture payloads don't contain newlines.
        txt.append("message = ").append(qr.message).append('\n');
        txt.append("payload = ").append(fx.payload).append('\n');
        txt.append("module_pixels = 4\n");
        txt.append("border_modules = ").append(gen.getBorderModule()).append('\n');
        txt.append("image_size = ").append(gray.width).append('\n');
        txt.append("num_modules = ").append(numModules).append('\n');
        txt.append("maskBits = ").append(maskValue).append('\n');
        txt.append("ppCorner = ").append(fmtPolygon(qr.ppCorner)).append('\n');
        txt.append("ppRight = ").append(fmtPolygon(qr.ppRight)).append('\n');
        txt.append("ppDown = ").append(fmtPolygon(qr.ppDown)).append('\n');
        java.nio.file.Files.writeString(new File(outDir, fx.name + ".txt").toPath(), txt.toString());
        System.out.println("Wrote " + fx.name + " (mask " + maskValue + ")");
    }

    /**
     * 12 base fixtures replacing the prior Python-generated set. Names
     * intentionally match `tests/fixtures/qr/*.png` so the existing C++
     * tests load them unchanged.
     */
    static List<Fixture> baseFixtures() {
        List<Fixture> out = new ArrayList<>();
        out.add(new Fixture("v1_M_numeric_8", 1, QrCode.ErrorLevel.M,
                QrCodeMaskPattern.M011, "01234567", Fixture.Mode.NUMERIC));
        out.add(new Fixture("v1_L_numeric_short", 1, QrCode.ErrorLevel.L,
                null, "12345", Fixture.Mode.NUMERIC));
        out.add(new Fixture("v1_H_numeric_short", 1, QrCode.ErrorLevel.H,
                null, "1234", Fixture.Mode.NUMERIC));
        out.add(new Fixture("v2_M_alphanum_HELLO", 2, QrCode.ErrorLevel.M,
                QrCodeMaskPattern.M011, "HELLO", Fixture.Mode.ALPHANUMERIC));
        out.add(new Fixture("v2_L_alphanum_long", 2, QrCode.ErrorLevel.L,
                null, "01234567ABCD", Fixture.Mode.ALPHANUMERIC));
        out.add(new Fixture("v2_M_byte_short", 2, QrCode.ErrorLevel.M,
                QrCodeMaskPattern.M011, "Pp4/", Fixture.Mode.BYTE));
        out.add(new Fixture("v3_M_mixed", 3, QrCode.ErrorLevel.M,
                null, "1235AFefg", Fixture.Mode.BYTE));
        out.add(new Fixture("v5_M_alphanum", 5, QrCode.ErrorLevel.M,
                null, "ALPHANUMTESTV5", Fixture.Mode.ALPHANUMERIC));
        out.add(new Fixture("v7_M_alphanum", 7, QrCode.ErrorLevel.M,
                null, "VERSION7TEST", Fixture.Mode.ALPHANUMERIC));
        out.add(new Fixture("v10_M_alphanum", 10, QrCode.ErrorLevel.M,
                null, "VERSIONTENISGOOD", Fixture.Mode.ALPHANUMERIC));
        out.add(new Fixture("v20_M_alphanum", 20, QrCode.ErrorLevel.M,
                null, "VERSIONTWENTYTEST", Fixture.Mode.ALPHANUMERIC));
        out.add(new Fixture("v40_M_numeric", 40, QrCode.ErrorLevel.M,
                null, "12345678901234567890123456789012345678901234567890",
                Fixture.Mode.NUMERIC));
        return out;
    }

    /** Codex review fix-up #8: 5 versions × 4 ECC × 8 masks = 160 cases. */
    static List<Fixture> fullSimpleMatrix() {
        List<Fixture> out = new ArrayList<>();
        int[] versions = {1, 2, 7, 20, 40};
        QrCode.ErrorLevel[] errors = {
                QrCode.ErrorLevel.L, QrCode.ErrorLevel.M,
                QrCode.ErrorLevel.Q, QrCode.ErrorLevel.H};
        QrCodeMaskPattern[] masks = {
                QrCodeMaskPattern.M000, QrCodeMaskPattern.M001,
                QrCodeMaskPattern.M010, QrCodeMaskPattern.M011,
                QrCodeMaskPattern.M100, QrCodeMaskPattern.M101,
                QrCodeMaskPattern.M110, QrCodeMaskPattern.M111};
        for (int v : versions) {
            for (QrCode.ErrorLevel e : errors) {
                for (QrCodeMaskPattern m : masks) {
                    String name = String.format("full_v%d_%s_M%s",
                            v, e.name(),
                            Integer.toBinaryString(0b1000 | maskBits(m)).substring(1));
                    out.add(new Fixture(name, v, e, m, "01234567",
                            Fixture.Mode.NUMERIC));
                }
            }
        }
        return out;
    }

    public static void main(String[] args) throws IOException {
        File outDir;
        if (args.length >= 1) {
            outDir = new File(args[0]);
        } else {
            // Default: relative to this Gradle project root.
            outDir = new File("../../tests/fixtures/qr");
        }
        if (!outDir.exists() && !outDir.mkdirs()) {
            throw new IOException("Cannot create output directory: " + outDir);
        }
        System.out.println("Writing fixtures to: " + outDir.getAbsolutePath());

        List<Fixture> all = new ArrayList<>();
        all.addAll(baseFixtures());
        all.addAll(fullSimpleMatrix());

        for (Fixture fx : all) {
            try {
                emit(fx, outDir);
            } catch (Exception e) {
                System.err.println("FAILED " + fx.name + ": " + e);
                e.printStackTrace(System.err);
                throw e;
            }
        }
        System.out.println("Done: " + all.size() + " fixtures written.");
    }
}
