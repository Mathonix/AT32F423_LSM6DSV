"""Preserve searchable text and navigation, but draw visible text as vector paths.

This avoids Chinese CID/CFF font rendering dependencies in embedded PDF viewers.
Requires pypdf and an installed Ghostscript; never changes the source PDF.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from pypdf import PdfReader, PdfWriter
from pypdf.generic import ContentStream, FloatObject, NameObject, NumberObject


def make_compatible(source: Path, destination: Path, gs: Path) -> None:
    if source.resolve() == destination.resolve():
        raise ValueError("Source and destination must differ")
    environment = os.environ.copy()
    # TeX Live's bundled executable needs its own resource search path.
    root = gs.parent.parent
    if (root / "Resource" / "Init").is_dir():
        environment["GS_LIB"] = os.pathsep.join(
            str(root / directory)
            for directory in ("Resource/Init", "lib", "Resource", "kanji", "fonts")
        )
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pdf-outline-") as temporary:
        scratch = Path(temporary)
        # ASCII filenames also work with older Windows Ghostscript builds.
        shutil.copyfile(source, scratch / "input.pdf")
        subprocess.run(
            [str(gs), "-q", "-dBATCH", "-dNOPAUSE", "-sDEVICE=pdfwrite",
             "-dNoOutputFonts", "-dPreserveAnnots=false", "-dAutoRotatePages=/None",
             "-dCompatibilityLevel=1.7", "-sOutputFile=outlines.pdf", "input.pdf"],
            cwd=scratch, env=environment, check=True,
        )
        original = PdfReader(source)
        outlines = PdfReader(scratch / "outlines.pdf")
        if len(original.pages) != len(outlines.pages):
            raise ValueError("Outline conversion changed page count")
        writer = PdfWriter()
        writer.clone_document_from_reader(original)
        for page, appearance in zip(writer.pages, outlines.pages):
            if tuple(page.mediabox) != tuple(appearance.mediabox):
                # Ghostscript may round dimensions by a fraction of a point.
                if any(abs(float(a) - float(b)) > 0.02
                       for a, b in zip(page.mediabox, appearance.mediabox)):
                    raise ValueError("Outline conversion changed page size")
            # Text remains selectable/searchable, but is never painted by a font.
            content = ContentStream(page.get_contents(), writer)
            operations = []
            for operands, operator in content.operations:
                if operator == b"Tr":
                    operands = [NumberObject(3)]
                operations.append((operands, operator))
                if operator == b"BT":
                    operations.append(([NumberObject(3)], b"Tr"))
            content.operations = operations
            page.replace_contents(content)
            # Cover original graphics, then overlay exact vector outlines/images.
            drawn = ContentStream(appearance.get_contents(), outlines)
            width, height = appearance.mediabox.width, appearance.mediabox.height
            drawn.operations = [([], b"q"), ([NumberObject(1)], b"g"),
                                ([NumberObject(0), NumberObject(0), FloatObject(width), FloatObject(height)], b"re"),
                                ([], b"f"), ([], b"Q")] + drawn.operations
            appearance[NameObject("/Contents")] = drawn
            page.merge_page(appearance, over=True)
            page.compress_content_streams()
        with destination.open("wb") as stream:
            writer.write(stream)
        result = PdfReader(destination)
        for index, (before, after) in enumerate(zip(original.pages, result.pages), 1):
            # pypdf may infer extra line breaks from the appended vector paths.
            # Compare text content without renderer-inferred whitespace.
            if "".join(before.extract_text().split()) != "".join(after.extract_text().split()):
                raise ValueError(f"Searchable text changed on page {index}")
            before_annotations = before.get("/Annots")
            after_annotations = after.get("/Annots")
            if (len(before_annotations.get_object()) if before_annotations else 0) != (
                len(after_annotations.get_object()) if after_annotations else 0
            ):
                raise ValueError(f"Navigation annotations changed on page {index}")
        if len(original.outline) != len(result.outline):
            raise ValueError("Document outline changed")
        print(f"Verified {len(result.pages)} pages: text, annotations, outline preserved")
        print(f"Output: {destination} ({destination.stat().st_size} bytes)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--ghostscript", type=Path, required=True)
    arguments = parser.parse_args()
    make_compatible(arguments.source, arguments.destination, arguments.ghostscript)
