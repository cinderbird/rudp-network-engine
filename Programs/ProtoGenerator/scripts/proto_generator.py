import os
import argparse
import subprocess
import shutil
import sys

def process_proto_file(protoc_path, proto_file, proto_source_root, generated_dir):

    relative_proto_path = os.path.relpath(proto_file, proto_source_root)
    print(f"Processing {relative_proto_path}...")

    result = subprocess.run(
        [
            protoc_path,
            "-I=.",
            f"--cpp_out=.",
            relative_proto_path
        ],
        cwd=proto_source_root,
        capture_output=True, text=True, encoding='utf-8'
    )

    if result.returncode != 0:
        print(f"Error: Processing {relative_proto_path} failed.")
        print(f"protoc stderr:\n{result.stderr}")
        sys.exit(1)

    base_name_with_subdir = os.path.splitext(relative_proto_path)[0]
    generated_h_path = os.path.join(proto_source_root, f"{base_name_with_subdir}.pb.h")
    generated_cc_path = os.path.join(proto_source_root, f"{base_name_with_subdir}.pb.cc")

    if not (os.path.exists(generated_h_path) and os.path.exists(generated_cc_path)):
        print(f"Error: Generated files for {relative_proto_path} not found.")
        print(f"Looked for: {generated_h_path} and {generated_cc_path}")
        sys.exit(1)

    dest_h_path = os.path.join(generated_dir, f"{base_name_with_subdir}.pb.h")
    dest_cc_path = os.path.join(generated_dir, f"{base_name_with_subdir}.pb.cc")

    os.makedirs(os.path.dirname(dest_h_path), exist_ok=True)

    shutil.move(generated_h_path, dest_h_path)
    shutil.move(generated_cc_path, dest_cc_path)

    print(f"{relative_proto_path} processed successfully.")

def main():
    parser = argparse.ArgumentParser(description="Generate C++ code from .proto files.")
    parser.add_argument("--protoc", required=True, help="Path to protoc.exe (C++ compiler)")
    parser.add_argument("--proto_file", required=True, help="Path to the specific .proto file to process")
    parser.add_argument("--source_root", required=True, help="Root directory of all .proto source files")
    parser.add_argument("--generated_dir", required=True, help="Output directory for generated .pb.h/.pb.cc files")
    args = parser.parse_args()

    process_proto_file(args.protoc, args.proto_file, args.source_root, args.generated_dir)

if __name__ == "__main__":
    main()
