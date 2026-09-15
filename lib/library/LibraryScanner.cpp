#include "LibraryScanner.h"

namespace knobify::library {

namespace {

ArtistId findOrAddArtist(LibraryIndex &index, const std::string &name) {
  for (const auto &artist : index.artists) {
    if (artist.name == name) return artist.id;
  }
  ArtistId id = static_cast<ArtistId>(index.artists.size());
  index.artists.push_back(Artist{id, name});
  return id;
}

AlbumId findOrAddAlbum(LibraryIndex &index, ArtistId artistId,
                       const std::string &title, uint16_t year,
                       bool *isNew) {
  for (const auto &album : index.albums) {
    if (album.artistId == artistId && album.title == title) {
      *isNew = false;
      return album.id;
    }
  }
  AlbumId id = static_cast<AlbumId>(index.albums.size());
  index.albums.push_back(Album{id, artistId, title, year});
  *isNew = true;
  return id;
}

// Directory directly containing `path`, keeping the full path (unlike
// TagReader's parentFolderName(), which only wants the bare name) --
// this is the album folder cover art gets cached against.
std::string parentDirectoryPath(const std::string &path) {
  auto lastSlash = path.find_last_of('/');
  if (lastSlash == std::string::npos) return "";
  return path.substr(0, lastSlash);
}

}  // namespace

LibraryIndex LibraryScanner::scan(FileLister &lister, FileOpener &opener,
                                   ScanProgressListener *progress) {
  LibraryIndex index;

  // Does NOT call lister.reset() -- callers are expected to have already
  // positioned the lister (e.g. computeSignature() already walked it once
  // for change-detection; re-walking here would pay for the SD card's
  // expensive recursive directory walk a second time for no reason).
  // Callers that just want a fresh scan with no prior walk should call
  // lister.reset() themselves before this.
  FileEntry entry;
  size_t scanned = 0;
  while (lister.next(entry)) {
    auto file = opener.open(entry.path);
    bool opened = static_cast<bool>(file);
    TagResult tags;  // Left default (found=false) if the file didn't open.
    if (opened) {
      tags = TagReader::read(*file, entry.path);

      ArtistId artistId = findOrAddArtist(index, tags.artist);
      bool isNewAlbum = false;
      AlbumId albumId =
          findOrAddAlbum(index, artistId, tags.album, tags.year, &isNewAlbum);

      TrackId trackId = static_cast<TrackId>(index.tracks.size());
      index.tracks.push_back(
          Track{trackId, albumId, tags.title, tags.trackNumber, entry.path,
                tags.discNumber});

      if (isNewAlbum && progress) {
        progress->onNewAlbum(parentDirectoryPath(entry.path), *file, tags);
      }
    }
    // Unreadable files are skipped rather than aborting the scan, but
    // still count toward progress -- the caller is showing "how far
    // through the file list are we", not "how many tracks found".
    ++scanned;
    if (progress) {
      progress->onFileScanned(scanned);
      progress->onFileResult(entry.path, opened, tags);
    }
  }

  return index;
}

}  // namespace knobify::library
