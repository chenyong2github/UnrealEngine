from PIL import Image

images = []
sizes = [255, 128, 64, 32, 48, 16]
for size in sizes:
    print(f"Loading ue_{size}.png")
    image = Image.open(f"ue_{size}.png")
    images.append(image)

print("Creating ../Unreal.ico")
images[0].save("../Unreal.ico", append_images=images)
