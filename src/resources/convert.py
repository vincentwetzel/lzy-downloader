import os
from PIL import Image

def convert_png_to_ico(input_path):
    if not os.path.exists(input_path):
        print(f"Error: {input_path} not found.")
        return

    img = Image.open(input_path)

    # Standard Windows icon sizes
    icon_sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]

    output_path = os.path.join(os.path.dirname(input_path), "LzyDownloader.ico") # Changed output filename
    img.save(output_path, format='ICO', sizes=icon_sizes)

    print(f"Success! Icon saved as: {output_path}")
    input("\nPress Enter to close...") # Pause at the end

if __name__ == "__main__":
    convert_png_to_ico("icon.