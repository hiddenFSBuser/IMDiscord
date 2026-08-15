import os

def process_files():
    root_dir = os.path.dirname(os.path.abspath(__file__))

    for root, dirs, files in os.walk(root_dir):
        for file in files:
            if file.endswith('.c'):
                old_path = os.path.join(root, file)
                new_path = os.path.join(root, file + 'pp') # .c -> .cpp
                
                print(f"Renaming: {file} -> {file}pp")
                os.rename(old_path, new_path)
                file = file + 'pp'
                current_path = new_path
            else:
                current_path = os.path.join(root, file)

            if file.endswith('.cpp'):
                try:
                    with open(current_path, 'r', encoding='utf-8') as f:
                        content = f.read()

                    if '#include "pch.h"' not in content:
                        print(f"Adding PCH to: {file}")
                        with open(current_path, 'w', encoding='utf-8') as f:
                            f.write('#include "pch.h"\n' + content)
                except Exception as e:
                    print(f"Error processing {file}: {e}")

if __name__ == "__main__":
    print("Starting conversion...")
    process_files()
    print("Done!")