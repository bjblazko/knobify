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
                       const std::string &title, uint16_t year) {
  for (const auto &album : index.albums) {
    if (album.artistId == artistId && album.title == title) return album.id;
  }
  AlbumId id = static_cast<AlbumId>(index.albums.size());
  index.albums.push_back(Album{id, artistId, title, year});
  return id;
}

}  // namespace

LibraryIndex LibraryScanner::scan(FileLister &lister, FileOpener &opener,
                                   ScanProgressListener *progress) {
  LibraryIndex index;

  lister.reset();
  FileEntry entry;
  size_t scanned = 0;
  while (lister.next(entry)) {
    auto file = opener.open(entry.path);
    bool opened = static_cast<bool>(file);
    TagResult tags;  // Left default (found=false) if the file didn't open.
    if (opened) {
      tags = TagReader::read(*file, entry.path);

      ArtistId artistId = findOrAddArtist(index, tags.artist);
      AlbumId albumId = findOrAddAlbum(index, artistId, tags.album, tags.year);

      TrackId trackId = static_cast<TrackId>(index.tracks.size());
      index.tracks.push_back(
          Track{trackId, albumId, tags.title, tags.trackNumber, entry.path});
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
