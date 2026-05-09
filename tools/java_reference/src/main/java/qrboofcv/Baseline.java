package qrboofcv;

import boofcv.abst.fiducial.QrCodePreciseDetector;
import boofcv.alg.fiducial.qrcode.QrCode;
import boofcv.factory.fiducial.FactoryFiducial;
import boofcv.io.image.UtilImageIO;
import boofcv.struct.image.GrayU8;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;

import georegression.struct.point.Point2D_F64;
import georegression.struct.shapes.Polygon2D_F64;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.stream.Stream;

/**
 * Run BoofCV's QR detector across an image dataset and dump per-image JSON
 * (ground truth + detections + decoded payload + wall-clock time).
 *
 * <p>Usage: {@code Baseline <datasetRoot> <outputDir>}.
 * The output directory mirrors {@code datasetRoot}'s layout, with one
 * {@code <basename>.json} per image plus a top-level {@code summary.json}.
 */
public final class Baseline {

    private static final String BOOFCV_VERSION = "1.3.0";

    public static void main(String[] args) throws IOException {
        if (args.length < 2) {
            System.err.println("Usage: Baseline <datasetRoot> <outputDir>");
            System.exit(2);
        }
        Path datasetRoot = Paths.get(args[0]).toAbsolutePath().normalize();
        Path outRoot = Paths.get(args[1]).toAbsolutePath().normalize();
        Files.createDirectories(outRoot);

        List<Path> images = collectImages(datasetRoot);
        System.out.printf("Found %d images under %s%n", images.size(), datasetRoot);

        QrCodePreciseDetector<GrayU8> detector =
            FactoryFiducial.qrcode(null, GrayU8.class);

        // JIT warmup so the first timed run isn't an outlier.
        int warmupN = Math.min(5, images.size());
        for (int i = 0; i < warmupN; i++) {
            GrayU8 g = UtilImageIO.loadImage(images.get(i).toString(), GrayU8.class);
            if (g != null) detector.process(g);
        }
        System.out.printf("Warmed up on %d images%n", warmupN);

        ObjectMapper mapper = new ObjectMapper();
        mapper.enable(SerializationFeature.INDENT_OUTPUT);

        List<Map<String, Object>> records = new ArrayList<>(images.size());
        long globalStart = System.nanoTime();
        int idx = 0;
        for (Path img : images) {
            idx++;
            Map<String, Object> rec = processOne(img, datasetRoot, detector);
            records.add(rec);

            Path rel = datasetRoot.relativize(img);
            Path outFile = outRoot.resolve(rel)
                .resolveSibling(stripExt(rel.getFileName().toString()) + ".json");
            Files.createDirectories(outFile.getParent());
            mapper.writeValue(outFile.toFile(), rec);

            if (idx % 50 == 0 || idx == images.size()) {
                System.out.printf("[%d/%d] processed%n", idx, images.size());
            }
        }
        long globalElapsedMs = (System.nanoTime() - globalStart) / 1_000_000L;

        Map<String, Object> meta = new LinkedHashMap<>();
        meta.put("dataset_root", datasetRoot.toString());
        meta.put("output_root", outRoot.toString());
        meta.put("boofcv_version", BOOFCV_VERSION);
        meta.put("image_count", images.size());
        meta.put("warmup_images", warmupN);
        meta.put("total_elapsed_ms", globalElapsedMs);
        meta.put("records", records);

        Path summaryFile = outRoot.resolve("summary.json");
        mapper.writeValue(summaryFile.toFile(), meta);
        System.out.printf("Wrote %s (%d records, %d ms total)%n",
            summaryFile, records.size(), globalElapsedMs);
    }

    private static List<Path> collectImages(Path root) throws IOException {
        List<Path> images = new ArrayList<>();
        try (Stream<Path> walk = Files.walk(root)) {
            walk.filter(Files::isRegularFile)
                .filter(p -> {
                    String n = p.getFileName().toString().toLowerCase(Locale.ROOT);
                    return n.endsWith(".jpg") || n.endsWith(".jpeg") || n.endsWith(".png");
                })
                .sorted()
                .forEach(images::add);
        }
        return images;
    }

    private static String stripExt(String name) {
        int dot = name.lastIndexOf('.');
        return dot >= 0 ? name.substring(0, dot) : name;
    }

    private static Map<String, Object> processOne(Path image,
                                                  Path datasetRoot,
                                                  QrCodePreciseDetector<GrayU8> detector) {
        Path rel = datasetRoot.relativize(image);
        String relStr = rel.toString().replace('\\', '/');
        String[] segs = relStr.split("/");
        // For boofcv-qrcodes layout the relative path is e.g. "qrcodes/detection/nominal/image001.jpg".
        // Walk up to the second-to-last segment for category, third-to-last for subset.
        String category = segs.length >= 2 ? segs[segs.length - 2] : "";
        String subset = segs.length >= 3 ? segs[segs.length - 3] : "";

        Map<String, Object> rec = new LinkedHashMap<>();
        rec.put("image_path", relStr);
        rec.put("subset", subset);
        rec.put("category", category);

        GrayU8 g = UtilImageIO.loadImage(image.toString(), GrayU8.class);
        if (g == null) {
            rec.put("error", "load_failed");
            return rec;
        }
        rec.put("image_width", g.width);
        rec.put("image_height", g.height);

        long t0 = System.nanoTime();
        detector.process(g);
        long elapsedNs = System.nanoTime() - t0;
        rec.put("elapsed_ms", elapsedNs / 1_000_000.0);

        rec.put("ground_truth", parseGroundTruth(image));
        rec.put("detections", serialiseDetections(detector.getDetections()));
        rec.put("failures", serialiseDetections(detector.getFailures()));
        return rec;
    }

    /**
     * Parse the BoofCV-format sidecar .txt next to {@code image}.
     *
     * <p>The dataset uses two interchangeable layouts; both are accepted here:
     * <pre>
     * # comments...
     * SETS
     * x1 y1 x2 y2 x3 y3 x4 y4   (one line per QR code)
     * ...
     * </pre>
     * and
     * <pre>
     * # comments...
     * x1 y1
     * x2 y2
     * x3 y3
     * x4 y4
     * (every 4 lines = 1 QR)
     * </pre>
     * The decoder-only subset adds:
     * <pre>
     * MESSAGE
     * &lt;payload text, may be multi-line&gt;
     * EOF
     * </pre>
     *
     * Implementation: collect every numeric token from non-comment,
     * non-keyword lines, then chunk by 8 floats = 4 corners per QR.
     */
    private static List<Map<String, Object>> parseGroundTruth(Path image) {
        Path txt = image.resolveSibling(stripExt(image.getFileName().toString()) + ".txt");
        List<Map<String, Object>> out = new ArrayList<>();
        if (!Files.isRegularFile(txt)) return out;

        try {
            List<String> lines = Files.readAllLines(txt, StandardCharsets.UTF_8);
            List<Double> nums = new ArrayList<>();
            boolean inMessage = false;
            boolean sawMessageKeyword = false;
            StringBuilder msgBuf = new StringBuilder();
            StringBuilder fallbackBuf = new StringBuilder();
            boolean coordRegionHadNonNumeric = false;

            for (String raw : lines) {
                String line = raw.trim();
                if (line.isEmpty()) continue;
                if (line.startsWith("#")) continue;
                String upper = line.toUpperCase(Locale.ROOT);

                if (upper.equals("SETS")) {
                    inMessage = false;
                    continue;
                }
                if (upper.equals("MESSAGE")) {
                    inMessage = true;
                    sawMessageKeyword = true;
                    msgBuf.setLength(0);
                    continue;
                }
                if (upper.equals("EOF") || upper.equals("END")) {
                    inMessage = false;
                    continue;
                }

                if (inMessage) {
                    if (msgBuf.length() > 0) msgBuf.append('\n');
                    msgBuf.append(line);
                    continue;
                }

                if (fallbackBuf.length() > 0) fallbackBuf.append('\n');
                fallbackBuf.append(line);

                String[] parts = line.split("\\s+");
                for (String tok : parts) {
                    if (tok.isEmpty()) continue;
                    try {
                        nums.add(Double.parseDouble(tok));
                    } catch (NumberFormatException nfe) {
                        coordRegionHadNonNumeric = true;
                    }
                }
            }

            // Decoding subset: a plain-text .txt with the expected payload, no
            // SETS/MESSAGE markers and no parseable coords. Use the whole
            // non-comment content as the payload.
            if (!sawMessageKeyword && nums.size() < 8 && coordRegionHadNonNumeric
                    && fallbackBuf.length() > 0) {
                msgBuf.setLength(0);
                msgBuf.append(fallbackBuf);
                nums.clear();
            }

            int complete = nums.size() / 8;
            for (int q = 0; q < complete; q++) {
                double[][] corners = new double[4][2];
                for (int i = 0; i < 4; i++) {
                    corners[i][0] = nums.get(q * 8 + i * 2);
                    corners[i][1] = nums.get(q * 8 + i * 2 + 1);
                }
                Map<String, Object> entry = new LinkedHashMap<>();
                entry.put("corners", corners);
                out.add(entry);
            }

            if (msgBuf.length() > 0) {
                String message = msgBuf.toString();
                if (out.isEmpty()) {
                    Map<String, Object> entry = new LinkedHashMap<>();
                    entry.put("message", message);
                    out.add(entry);
                } else {
                    out.get(0).put("message", message);
                }
            }
        } catch (IOException e) {
            // Ignore — leave ground truth empty.
        }
        return out;
    }

    private static List<Map<String, Object>> serialiseDetections(List<QrCode> qrs) {
        List<Map<String, Object>> out = new ArrayList<>();
        for (QrCode qr : qrs) {
            Map<String, Object> e = new LinkedHashMap<>();
            e.put("corners", polygonToArray(qr.bounds));
            e.put("pp_corner", polygonToArray(qr.ppCorner));
            e.put("pp_right", polygonToArray(qr.ppRight));
            e.put("pp_down", polygonToArray(qr.ppDown));
            e.put("message", qr.message);
            e.put("byte_encoding", qr.byteEncoding);
            e.put("version", qr.version);
            e.put("total_bit_errors", qr.totalBitErrors);
            e.put("error_correction", qr.error == null ? null : qr.error.name());
            e.put("mode", qr.mode == null ? null : qr.mode.name());
            e.put("mask", qr.mask == null ? null : qr.mask.toString());
            e.put("failure_cause",
                qr.failureCause == null ? null : qr.failureCause.name());
            out.add(e);
        }
        return out;
    }

    private static double[][] polygonToArray(Polygon2D_F64 p) {
        double[][] out = new double[p.size()][2];
        for (int i = 0; i < p.size(); i++) {
            Point2D_F64 v = p.get(i);
            out[i][0] = v.x;
            out[i][1] = v.y;
        }
        return out;
    }

    private Baseline() {}
}
