import SwiftUI
import PhotosUI
import UIKit

struct CustomizationSettingsView: View {
    @AppStorage("profile.localPicturePath") private var localPicturePath = ""
    @State private var selectedPhoto: PhotosPickerItem?
    @State private var isLoadingPhoto = false
    @State private var photoError: String?

    var body: some View {
        NXPage {
            VStack(alignment: .leading, spacing: 12) {
                Text("Profile")
                    .font(.system(size: 20, weight: .semibold))
                    .foregroundStyle(NXTheme.text)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.horizontal, 18)
                    .padding(.top, 8)

                HStack(spacing: 18) {
                    ProfileAvatarView(size: 76)

                    VStack(alignment: .leading, spacing: 5) {
                        Text("Profile Picture")
                            .font(.system(size: 18, weight: .medium))
                            .foregroundStyle(NXTheme.text)
                        Text(localPicturePath.isEmpty ? "Choose an image stored on this device." : "Your local profile picture is active.")
                            .font(.system(size: 14))
                            .foregroundStyle(NXTheme.secondary)
                    }
                    Spacer(minLength: 0)
                }
                .padding(.horizontal, 18)
                .padding(.vertical, 12)

                PhotosPicker(selection: $selectedPhoto, matching: .images, photoLibrary: .shared()) {
                    NXRowLabel("Select Local Profile Picture") {
                        if isLoadingPhoto {
                            ProgressView()
                        } else {
                            Image(systemName: "photo.on.rectangle")
                                .font(.system(size: 18))
                                .foregroundStyle(NXTheme.accent)
                        }
                    }
                }
                .disabled(isLoadingPhoto)
                .onChange(of: selectedPhoto) { _, item in
                    guard let item else { return }
                    Task { await importPhoto(item) }
                }

                if !localPicturePath.isEmpty {
                    Button(role: .destructive) {
                        removePhoto()
                    } label: {
                        NXRowLabel("Remove Profile Picture") {
                            Image(systemName: "trash")
                                .foregroundStyle(.red)
                        }
                    }
                    .buttonStyle(.plain)
                }

                if let photoError {
                    Text(photoError)
                        .font(.system(size: 13))
                        .foregroundStyle(.red)
                        .padding(.horizontal, 18)
                }

                NXNote("Your selected picture is saved locally in Nexilium's app storage. It is not uploaded.")
            }
        }
    }

    @MainActor
    private func importPhoto(_ item: PhotosPickerItem) async {
        isLoadingPhoto = true
        photoError = nil
        defer {
            isLoadingPhoto = false
            selectedPhoto = nil
        }

        do {
            guard let data = try await item.loadTransferable(type: Data.self),
                  let image = UIImage(data: data),
                  let jpegData = image.jpegData(compressionQuality: 0.90) else {
                photoError = "This image couldn't be opened. Please choose another photo."
                return
            }

            let directory = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
                .appendingPathComponent("Profile", isDirectory: true)
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)

            let destination = directory.appendingPathComponent("profile-picture.jpg")
            try jpegData.write(to: destination, options: .atomic)
            localPicturePath = destination.path
        } catch {
            photoError = "Couldn't save this picture. Please try again."
        }
    }

    private func removePhoto() {
        if !localPicturePath.isEmpty {
            try? FileManager.default.removeItem(atPath: localPicturePath)
        }
        localPicturePath = ""
        photoError = nil
    }
}

struct ProfileAvatarView: View {
    var size: CGFloat

    @AppStorage("profile.localPicturePath") private var localPicturePath = ""

    var body: some View {
        Group {
            if !localPicturePath.isEmpty,
               let image = UIImage(contentsOfFile: localPicturePath) {
                Image(uiImage: image)
                    .resizable()
                    .scaledToFill()
            } else {
                Image(systemName: "person.fill")
                    .resizable()
                    .scaledToFit()
                    .padding(size * 0.25)
                    .foregroundStyle(Color.white)
                    .background(Circle().fill(NXTheme.secondary.opacity(0.75)))
            }
        }
        .frame(width: size, height: size)
        .clipShape(Circle())
        .overlay(Circle().stroke(Color.white, lineWidth: 2))
        .accessibilityLabel("Profile picture")
    }
}
