// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#include "shares/share_file_client.hpp"

#include "common/concurrent_transfer.hpp"
#include "common/constants.hpp"
#include "common/crypt.hpp"
#include "common/file_io.hpp"
#include "common/reliable_stream.hpp"
#include "common/shared_key_policy.hpp"
#include "common/storage_common.hpp"
#include "common/storage_per_retry_policy.hpp"
#include "common/storage_version.hpp"
#include "azure/core/credentials/policy/policies.hpp"
#include "azure/core/http/curl/curl.hpp"
#include "shares/share_constants.hpp"

namespace Azure { namespace Storage { namespace Files { namespace Shares {

  FileClient FileClient::CreateFromConnectionString(
      const std::string& connectionString,
      const std::string& shareName,
      const std::string& filePath,
      const FileClientOptions& options)
  {
    auto parsedConnectionString = Azure::Storage::Details::ParseConnectionString(connectionString);
    auto fileUri = std::move(parsedConnectionString.FileServiceUri);
    fileUri.AppendPath(shareName, true);
    fileUri.AppendPath(filePath, true);

    if (parsedConnectionString.KeyCredential)
    {
      return FileClient(fileUri.ToString(), parsedConnectionString.KeyCredential, options);
    }
    else
    {
      return FileClient(fileUri.ToString(), options);
    }
  }

  FileClient::FileClient(
      const std::string& shareFileUri,
      std::shared_ptr<SharedKeyCredential> credential,
      const FileClientOptions& options)
      : m_shareFileUri(shareFileUri)
  {

    std::vector<std::unique_ptr<Azure::Core::Http::HttpPolicy>> policies;
    policies.emplace_back(std::make_unique<Azure::Core::Http::TelemetryPolicy>(
        Azure::Storage::Details::c_FileServicePackageName, FileServiceVersion));
    policies.emplace_back(std::make_unique<Azure::Core::Http::RequestIdPolicy>());
    for (const auto& p : options.PerOperationPolicies)
    {
      policies.emplace_back(p->Clone());
    }
    policies.emplace_back(
        std::make_unique<Azure::Core::Http::RetryPolicy>(Azure::Core::Http::RetryOptions()));
    for (const auto& p : options.PerRetryPolicies)
    {
      policies.emplace_back(p->Clone());
    }
    policies.emplace_back(std::make_unique<StoragePerRetryPolicy>());
    policies.emplace_back(std::make_unique<SharedKeyPolicy>(credential));
    policies.emplace_back(std::make_unique<Azure::Core::Http::TransportPolicy>(
        std::make_shared<Azure::Core::Http::CurlTransport>()));
    m_pipeline = std::make_shared<Azure::Core::Http::HttpPipeline>(policies);
  }

  FileClient::FileClient(
      const std::string& shareFileUri,
      std::shared_ptr<Core::Credentials::TokenCredential> credential,
      const FileClientOptions& options)
      : m_shareFileUri(shareFileUri)
  {
    std::vector<std::unique_ptr<Azure::Core::Http::HttpPolicy>> policies;
    policies.emplace_back(std::make_unique<Azure::Core::Http::TelemetryPolicy>(
        Azure::Storage::Details::c_FileServicePackageName, FileServiceVersion));
    policies.emplace_back(std::make_unique<Azure::Core::Http::RequestIdPolicy>());
    for (const auto& p : options.PerOperationPolicies)
    {
      policies.emplace_back(p->Clone());
    }
    policies.emplace_back(
        std::make_unique<Azure::Core::Http::RetryPolicy>(Azure::Core::Http::RetryOptions()));
    for (const auto& p : options.PerRetryPolicies)
    {
      policies.emplace_back(p->Clone());
    }
    policies.emplace_back(std::make_unique<StoragePerRetryPolicy>());
    policies.emplace_back(
        std::make_unique<Core::Credentials::Policy::BearerTokenAuthenticationPolicy>(
            credential, Azure::Storage::Details::c_StorageScope));
    policies.emplace_back(std::make_unique<Azure::Core::Http::TransportPolicy>(
        std::make_shared<Azure::Core::Http::CurlTransport>()));
    m_pipeline = std::make_shared<Azure::Core::Http::HttpPipeline>(policies);
  }

  FileClient::FileClient(const std::string& shareFileUri, const FileClientOptions& options)
      : m_shareFileUri(shareFileUri)
  {
    std::vector<std::unique_ptr<Azure::Core::Http::HttpPolicy>> policies;
    policies.emplace_back(std::make_unique<Azure::Core::Http::TelemetryPolicy>(
        Azure::Storage::Details::c_FileServicePackageName, FileServiceVersion));
    policies.emplace_back(std::make_unique<Azure::Core::Http::RequestIdPolicy>());
    for (const auto& p : options.PerOperationPolicies)
    {
      policies.emplace_back(p->Clone());
    }
    policies.emplace_back(
        std::make_unique<Azure::Core::Http::RetryPolicy>(Azure::Core::Http::RetryOptions()));
    for (const auto& p : options.PerRetryPolicies)
    {
      policies.emplace_back(p->Clone());
    }
    policies.emplace_back(std::make_unique<StoragePerRetryPolicy>());
    policies.emplace_back(std::make_unique<Azure::Core::Http::TransportPolicy>(
        std::make_shared<Azure::Core::Http::CurlTransport>()));
    m_pipeline = std::make_shared<Azure::Core::Http::HttpPipeline>(policies);
  }

  FileClient FileClient::WithSnapshot(const std::string& snapshot) const
  {
    FileClient newClient(*this);
    if (snapshot.empty())
    {
      newClient.m_shareFileUri.RemoveQuery(Details::c_ShareSnapshotQueryParameter);
    }
    else
    {
      newClient.m_shareFileUri.AppendQuery(Details::c_ShareSnapshotQueryParameter, snapshot);
    }
    return newClient;
  }

  Azure::Core::Response<CreateFileResult> FileClient::Create(
      int64_t fileSize,
      const CreateFileOptions& options) const
  {
    auto protocolLayerOptions = ShareRestClient::File::CreateOptions();
    protocolLayerOptions.Metadata = options.Metadata;
    protocolLayerOptions.FileAttributes = FileAttributesToString(options.SmbProperties.Attributes);
    if (protocolLayerOptions.FileAttributes.empty())
    {
      protocolLayerOptions.FileAttributes = FileAttributesToString(FileAttributes::None);
    }
    if (options.SmbProperties.FileCreationTime.HasValue())
    {
      protocolLayerOptions.FileCreationTime = options.SmbProperties.FileCreationTime.GetValue();
    }
    else
    {
      protocolLayerOptions.FileCreationTime = std::string(c_FileDefaultTimeValue);
    }
    if (options.SmbProperties.FileLastWriteTime.HasValue())
    {
      protocolLayerOptions.FileLastWriteTime = options.SmbProperties.FileLastWriteTime.GetValue();
    }
    else
    {
      protocolLayerOptions.FileLastWriteTime = std::string(c_FileDefaultTimeValue);
    }
    if (options.FilePermission.HasValue())
    {
      protocolLayerOptions.FilePermission = options.FilePermission;
    }
    else if (options.SmbProperties.FilePermissionKey.HasValue())
    {
      protocolLayerOptions.FilePermissionKey = options.SmbProperties.FilePermissionKey;
    }
    else
    {
      protocolLayerOptions.FilePermission = std::string(c_FileInheritPermission);
    }
    protocolLayerOptions.XMsContentLength = fileSize;
    if (!options.HttpHeaders.ContentType.empty())
    {
      protocolLayerOptions.FileContentType = options.HttpHeaders.ContentType;
    }
    if (!options.HttpHeaders.ContentEncoding.empty())
    {
      protocolLayerOptions.FileContentEncoding = options.HttpHeaders.ContentEncoding;
    }
    if (!options.HttpHeaders.ContentLanguage.empty())
    {
      protocolLayerOptions.FileContentLanguage = options.HttpHeaders.ContentLanguage;
    }
    if (!options.HttpHeaders.CacheControl.empty())
    {
      protocolLayerOptions.FileCacheControl = options.HttpHeaders.CacheControl;
    }
    if (!options.HttpHeaders.ContentDisposition.empty())
    {
      protocolLayerOptions.FileContentDisposition = options.HttpHeaders.ContentDisposition;
    }
    protocolLayerOptions.FileContentMD5 = options.FileContentMD5;
    protocolLayerOptions.LeaseIdOptional = options.AccessConditions.LeaseId;
    return ShareRestClient::File::Create(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);
  }

  Azure::Core::Response<DeleteFileResult> FileClient::Delete(const DeleteFileOptions& options) const
  {
    auto protocolLayerOptions = ShareRestClient::File::DeleteOptions();
    protocolLayerOptions.LeaseIdOptional = options.AccessConditions.LeaseId;
    return ShareRestClient::File::Delete(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);
  }

  Azure::Core::Response<DownloadFileResult> FileClient::Download(
      const DownloadFileOptions& options) const
  {
    auto protocolLayerOptions = ShareRestClient::File::DownloadOptions();
    if (options.Offset.HasValue())
    {
      if (options.Length.HasValue())
      {
        protocolLayerOptions.Range = std::string("bytes=")
            + std::to_string(options.Offset.GetValue()) + std::string("-")
            + std::to_string(options.Offset.GetValue() + options.Length.GetValue() - 1);
      }
      else
      {
        protocolLayerOptions.Range
            = std::string("bytes=") + std::to_string(options.Offset.GetValue()) + std::string("-");
      }
    }
    protocolLayerOptions.GetRangeContentMD5 = options.GetRangeContentMD5;
    protocolLayerOptions.LeaseIdOptional = options.AccessConditions.LeaseId;

    auto downloadResponse = ShareRestClient::File::Download(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);

    {
      // In case network failure during reading the body
      std::string eTag = downloadResponse->ETag;

      auto retryFunction
          = [this, options, eTag](
                const Azure::Core::Context& context,
                const HttpGetterInfo& retryInfo) -> std::unique_ptr<Azure::Core::Http::BodyStream> {
        unused(context);

        DownloadFileOptions newOptions = options;
        newOptions.Offset
            = (options.Offset.HasValue() ? options.Offset.GetValue() : 0) + retryInfo.Offset;
        newOptions.Length = options.Length;
        if (newOptions.Length.HasValue())
        {
          newOptions.Length = options.Length.GetValue() - retryInfo.Offset;
        }

        auto newResponse = Download(newOptions);
        if (eTag != newResponse->ETag)
        {
          throw std::runtime_error("File was changed during the download process.");
        }
        return std::move(Download(newOptions)->BodyStream);
      };

      ReliableStreamOptions reliableStreamOptions;
      reliableStreamOptions.MaxRetryRequests = Storage::Details::c_reliableStreamRetryCount;
      downloadResponse->BodyStream = std::make_unique<ReliableStream>(
          std::move(downloadResponse->BodyStream), reliableStreamOptions, retryFunction);
    }
    return downloadResponse;
  }

  Azure::Core::Response<StartCopyFileResult> FileClient::StartCopy(
      std::string copySource,
      const StartCopyFileOptions& options) const
  {
    auto protocolLayerOptions = ShareRestClient::File::StartCopyOptions();
    protocolLayerOptions.Metadata = options.Metadata;
    protocolLayerOptions.CopySource = std::move(copySource);
    protocolLayerOptions.FileCopyFileAttributes
        = FileAttributesToString(options.SmbProperties.Attributes);
    protocolLayerOptions.FileCopyFileCreationTime = options.SmbProperties.FileCreationTime;
    protocolLayerOptions.FileCopyFileLastWriteTime = options.SmbProperties.FileLastWriteTime;
    if (options.FilePermission.HasValue())
    {
      protocolLayerOptions.FilePermission = options.FilePermission;
    }
    else if (options.SmbProperties.FilePermissionKey.HasValue())
    {
      protocolLayerOptions.FilePermissionKey = options.SmbProperties.FilePermissionKey;
    }
    else
    {
      protocolLayerOptions.FilePermission = std::string(c_FileInheritPermission);
    }
    protocolLayerOptions.XMsFilePermissionCopyMode = options.FilePermissionCopyMode;
    protocolLayerOptions.FileCopyIgnoreReadOnly = options.IgnoreReadOnly;
    protocolLayerOptions.FileCopySetArchiveAttribute = options.SetArchiveAttribute;
    protocolLayerOptions.LeaseIdOptional = options.AccessConditions.LeaseId;
    return ShareRestClient::File::StartCopy(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);
  }

  Azure::Core::Response<AbortCopyFileResult> FileClient::AbortCopy(
      std::string copyId,
      const AbortCopyFileOptions& options) const
  {
    auto protocolLayerOptions = ShareRestClient::File::AbortCopyOptions();
    protocolLayerOptions.CopyId = std::move(copyId);
    protocolLayerOptions.LeaseIdOptional = options.AccessConditions.LeaseId;
    return ShareRestClient::File::AbortCopy(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);
  }

  Azure::Core::Response<GetFilePropertiesResult> FileClient::GetProperties(
      const GetFilePropertiesOptions& options) const
  {
    auto protocolLayerOptions = ShareRestClient::File::GetPropertiesOptions();
    protocolLayerOptions.LeaseIdOptional = options.AccessConditions.LeaseId;
    return ShareRestClient::File::GetProperties(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);
  }

  Azure::Core::Response<SetFilePropertiesResult> FileClient::SetProperties(
      FileShareHttpHeaders httpHeaders,
      FileShareSmbProperties smbProperties,
      const SetFilePropertiesOptions& options) const
  {
    auto protocolLayerOptions = ShareRestClient::File::SetHttpHeadersOptions();
    protocolLayerOptions.FileAttributes = FileAttributesToString(smbProperties.Attributes);
    if (smbProperties.FileCreationTime.HasValue())
    {
      protocolLayerOptions.FileCreationTime = smbProperties.FileCreationTime.GetValue();
    }
    else
    {
      protocolLayerOptions.FileCreationTime = std::string(c_FilePreserveSmbProperties);
    }
    if (smbProperties.FileLastWriteTime.HasValue())
    {
      protocolLayerOptions.FileLastWriteTime = smbProperties.FileLastWriteTime.GetValue();
    }
    else
    {
      protocolLayerOptions.FileLastWriteTime = std::string(c_FilePreserveSmbProperties);
    }
    protocolLayerOptions.XMsContentLength = options.NewSize;
    protocolLayerOptions.LeaseIdOptional = options.AccessConditions.LeaseId;
    if (options.FilePermission.HasValue())
    {
      protocolLayerOptions.FilePermission = options.FilePermission;
    }
    else if (smbProperties.FilePermissionKey.HasValue())
    {
      protocolLayerOptions.FilePermissionKey = smbProperties.FilePermissionKey;
    }
    else
    {
      protocolLayerOptions.FilePermission = std::string(c_FileInheritPermission);
    }

    if (!httpHeaders.ContentType.empty())
    {
      protocolLayerOptions.FileContentType = httpHeaders.ContentType;
    }
    if (!httpHeaders.ContentEncoding.empty())
    {
      protocolLayerOptions.FileContentEncoding = httpHeaders.ContentEncoding;
    }
    if (!httpHeaders.ContentLanguage.empty())
    {
      protocolLayerOptions.FileContentLanguage = httpHeaders.ContentLanguage;
    }
    if (!httpHeaders.CacheControl.empty())
    {
      protocolLayerOptions.FileCacheControl = httpHeaders.CacheControl;
    }
    if (!httpHeaders.ContentDisposition.empty())
    {
      protocolLayerOptions.FileContentDisposition = httpHeaders.ContentDisposition;
    }

    return ShareRestClient::File::SetHttpHeaders(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);
  }

  Azure::Core::Response<SetFileMetadataResult> FileClient::SetMetadata(
      const std::map<std::string, std::string>& metadata,
      const SetFileMetadataOptions& options) const
  {
    auto protocolLayerOptions = ShareRestClient::File::SetMetadataOptions();
    protocolLayerOptions.Metadata = metadata;
    protocolLayerOptions.LeaseIdOptional = options.AccessConditions.LeaseId;
    return ShareRestClient::File::SetMetadata(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);
  }

  Azure::Core::Response<UploadFileRangeResult> FileClient::UploadRange(
      Azure::Core::Http::BodyStream* content,
      int64_t offset,
      const UploadFileRangeOptions& options) const
  {
    auto protocolLayerOptions = ShareRestClient::File::UploadRangeOptions();
    protocolLayerOptions.XMsWrite = FileRangeWriteType::Update;
    protocolLayerOptions.ContentLength = content->Length();
    protocolLayerOptions.XMsRange = std::string("bytes=") + std::to_string(offset)
        + std::string("-") + std::to_string(offset + content->Length() - 1);
    protocolLayerOptions.ContentMD5 = options.ContentMD5;
    protocolLayerOptions.LeaseIdOptional = options.AccessConditions.LeaseId;
    return ShareRestClient::File::UploadRange(
        m_shareFileUri.ToString(), *content, *m_pipeline, options.Context, protocolLayerOptions);
  }

  Azure::Core::Response<UploadFileRangeFromUrlResult> FileClient::UploadRangeFromUrl(
      std::string sourceUrl,
      int64_t offset,
      int64_t length,
      const UploadFileRangeFromUrlOptions& options) const
  {
    auto protocolLayerOptions = ShareRestClient::File::UploadRangeFromUrlOptions();
    protocolLayerOptions.XMsWrite = FileRangeWriteFromUrlType::Update;
    protocolLayerOptions.CopySource = std::move(sourceUrl);
    protocolLayerOptions.ContentLength = length;
    protocolLayerOptions.TargetRange = std::string("bytes=") + std::to_string(offset)
        + std::string("-") + std::to_string(offset + length - 1);
    if (options.SourceOffset.HasValue())
    {
      if (options.SourceLength.HasValue())
      {
        protocolLayerOptions.TargetRange = std::string("bytes=")
            + std::to_string(options.SourceOffset.GetValue()) + std::string("-")
            + std::to_string(options.SourceOffset.GetValue() + options.SourceLength.GetValue() - 1);
      }
      else
      {
        protocolLayerOptions.TargetRange = std::string("bytes=")
            + std::to_string(options.SourceOffset.GetValue()) + std::string("-");
      }
    }
    protocolLayerOptions.SourceContentCrc64 = options.SourceContentCrc64;
    protocolLayerOptions.SourceIfMatchCrc64 = options.SourceIfMatchCrc64;
    protocolLayerOptions.SourceIfNoneMatchCrc64 = options.SourceIfNoneMatchCrc64;
    protocolLayerOptions.LeaseIdOptional = options.AccessConditions.LeaseId;
    return ShareRestClient::File::UploadRangeFromUrl(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);
  }

  Azure::Core::Response<ClearFileRangeResult> FileClient::ClearRange(
      int64_t offset,
      const ClearFileRangeOptions& options) const
  {
    auto protocolLayerOptions = ShareRestClient::File::UploadRangeOptions();
    protocolLayerOptions.XMsWrite = FileRangeWriteType::Clear;
    protocolLayerOptions.ContentLength = 0;
    if (options.Length.HasValue())
    {
      protocolLayerOptions.XMsRange = std::string("bytes=") + std::to_string(offset)
          + std::string("-") + std::to_string(offset + options.Length.GetValue() - 1);
    }
    else
    {
      protocolLayerOptions.XMsRange
          = std::string("bytes=") + std::to_string(offset) + std::string("-");
    }

    protocolLayerOptions.LeaseIdOptional = options.AccessConditions.LeaseId;
    return ShareRestClient::File::UploadRange(
        m_shareFileUri.ToString(),
        *Azure::Core::Http::NullBodyStream::GetNullBodyStream(),
        *m_pipeline,
        options.Context,
        protocolLayerOptions);
  }

  Azure::Core::Response<GetFileRangeListResult> FileClient::GetRangeList(
      const GetFileRangeListOptions& options) const
  {
    auto protocolLayerOptions = ShareRestClient::File::GetRangeListOptions();
    if (options.Offset.HasValue())
    {
      if (options.Length.HasValue())
      {
        protocolLayerOptions.XMsRange = std::string("bytes=")
            + std::to_string(options.Offset.GetValue()) + std::string("-")
            + std::to_string(options.Offset.GetValue() + options.Length.GetValue() - 1);
      }
      else
      {
        protocolLayerOptions.XMsRange
            = std::string("bytes=") + std::to_string(options.Offset.GetValue()) + std::string("-");
      }
    }

    protocolLayerOptions.LeaseIdOptional = options.AccessConditions.LeaseId;
    return ShareRestClient::File::GetRangeList(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);
  }

  Azure::Core::Response<ListFileHandlesSegmentedResult> FileClient::ListHandlesSegmented(
      const ListFileHandlesSegmentedOptions& options) const
  {
    auto protocolLayerOptions = ShareRestClient::File::ListHandlesOptions();
    protocolLayerOptions.Marker = options.Marker;
    protocolLayerOptions.MaxResults = options.MaxResults;
    auto result = ShareRestClient::File::ListHandles(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);
    ListFileHandlesSegmentedResult ret;
    ret.NextMarker = std::move(result->NextMarker);
    ret.HandleList = std::move(result->HandleList);

    return Azure::Core::Response<ListFileHandlesSegmentedResult>(
        std::move(ret), result.ExtractRawResponse());
  }

  Azure::Core::Response<ForceCloseFileHandlesResult> FileClient::ForceCloseHandles(
      const std::string& handleId,
      const ForceCloseFileHandlesOptions& options) const
  {
    auto protocolLayerOptions = ShareRestClient::File::ForceCloseHandlesOptions();
    protocolLayerOptions.HandleId = handleId;
    protocolLayerOptions.Marker = options.Marker;
    return ShareRestClient::File::ForceCloseHandles(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);
  }

  Azure::Core::Response<AcquireFileLeaseResult> FileClient::AcquireLease(
      const std::string& proposedLeaseId,
      const AcquireFileLeaseOptions& options) const
  {
    ShareRestClient::File::AcquireLeaseOptions protocolLayerOptions;
    protocolLayerOptions.ProposedLeaseIdOptional = proposedLeaseId;
    protocolLayerOptions.LeaseDuration = -1;
    return ShareRestClient::File::AcquireLease(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);
  }

  Azure::Core::Response<ChangeFileLeaseResult> FileClient::ChangeLease(
      const std::string& leaseId,
      const std::string& proposedLeaseId,
      const ChangeFileLeaseOptions& options) const
  {
    ShareRestClient::File::ChangeLeaseOptions protocolLayerOptions;
    protocolLayerOptions.LeaseIdRequired = leaseId;
    protocolLayerOptions.ProposedLeaseIdOptional = proposedLeaseId;
    return ShareRestClient::File::ChangeLease(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);
  }

  Azure::Core::Response<ReleaseFileLeaseResult> FileClient::ReleaseLease(
      const std::string& leaseId,
      const ReleaseFileLeaseOptions& options) const
  {
    ShareRestClient::File::ReleaseLeaseOptions protocolLayerOptions;
    protocolLayerOptions.LeaseIdRequired = leaseId;
    return ShareRestClient::File::ReleaseLease(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);
  }

  Azure::Core::Response<BreakFileLeaseResult> FileClient::BreakLease(
      const BreakFileLeaseOptions& options) const
  {
    ShareRestClient::File::BreakLeaseOptions protocolLayerOptions;
    return ShareRestClient::File::BreakLease(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);
  }

  Azure::Core::Response<DownloadFileToResult> FileClient::DownloadTo(
      uint8_t* buffer,
      std::size_t bufferSize,
      const DownloadFileToOptions& options) const
  {
    // Just start downloading using an initial chunk. If it's a small file, we'll get the whole
    // thing in one shot. If it's a large file, we'll get its full size in Content-Range and can
    // keep downloading it in chunks.
    int64_t firstChunkOffset = options.Offset.HasValue() ? options.Offset.GetValue() : 0;
    int64_t firstChunkLength = Details::c_FileDownloadDefaultChunkSize;
    if (options.InitialChunkSize.HasValue())
    {
      firstChunkLength = options.InitialChunkSize.GetValue();
    }
    if (options.Length.HasValue())
    {
      firstChunkLength = std::min(firstChunkLength, options.Length.GetValue());
    }

    DownloadFileOptions firstChunkOptions;
    firstChunkOptions.Context = options.Context;
    firstChunkOptions.Offset = options.Offset;
    if (firstChunkOptions.Offset.HasValue())
    {
      firstChunkOptions.Length = firstChunkLength;
    }

    auto firstChunk = Download(firstChunkOptions);

    int64_t fileSize;
    int64_t fileRangeSize;
    if (firstChunkOptions.Offset.HasValue())
    {
      fileSize = std::stoll(firstChunk->ContentRange.GetValue().substr(
          firstChunk->ContentRange.GetValue().find('/') + 1));
      fileRangeSize = fileSize - firstChunkOffset;
      if (options.Length.HasValue())
      {
        fileRangeSize = std::min(fileRangeSize, options.Length.GetValue());
      }
    }
    else
    {
      fileSize = firstChunk->BodyStream->Length();
      fileRangeSize = fileSize;
    }
    firstChunkLength = std::min(firstChunkLength, fileRangeSize);

    if (static_cast<std::size_t>(fileRangeSize) > bufferSize)
    {
      throw std::runtime_error(
          "buffer is not big enough, file range size is " + std::to_string(fileRangeSize));
    }

    int64_t bytesRead = Azure::Core::Http::BodyStream::ReadToCount(
        firstChunkOptions.Context, *(firstChunk->BodyStream), buffer, firstChunkLength);
    if (bytesRead != firstChunkLength)
    {
      throw std::runtime_error("error when reading body stream");
    }
    firstChunk->BodyStream.reset();

    auto returnTypeConverter = [](Azure::Core::Response<DownloadFileResult>& response) {
      DownloadFileToResult ret;
      ret.ETag = std::move(response->ETag);
      ret.LastModified = std::move(response->LastModified);
      ret.HttpHeaders = std::move(response->HttpHeaders);
      ret.Metadata = std::move(response->Metadata);
      ret.IsServerEncrypted = response->IsServerEncrypted;
      return Azure::Core::Response<DownloadFileToResult>(
          std::move(ret),
          std::make_unique<Azure::Core::Http::RawResponse>(std::move(response.GetRawResponse())));
    };
    auto ret = returnTypeConverter(firstChunk);

    // Keep downloading the remaining in parallel
    auto downloadChunkFunc
        = [&](int64_t offset, int64_t length, int64_t chunkId, int64_t numChunks) {
            DownloadFileOptions chunkOptions;
            chunkOptions.Context = options.Context;
            chunkOptions.Offset = offset;
            chunkOptions.Length = length;
            auto chunk = Download(chunkOptions);
            int64_t bytesRead = Azure::Core::Http::BodyStream::ReadToCount(
                chunkOptions.Context,
                *(chunk->BodyStream),
                buffer + (offset - firstChunkOffset),
                chunkOptions.Length.GetValue());
            if (bytesRead != chunkOptions.Length.GetValue())
            {
              throw std::runtime_error("error when reading body stream");
            }

            if (chunkId == numChunks - 1)
            {
              ret = returnTypeConverter(chunk);
            }
          };

    int64_t remainingOffset = firstChunkOffset + firstChunkLength;
    int64_t remainingSize = fileRangeSize - firstChunkLength;
    int64_t chunkSize;
    if (options.ChunkSize.HasValue())
    {
      chunkSize = options.ChunkSize.GetValue();
    }
    else
    {
      int64_t c_grainSize = 4 * 1024;
      chunkSize = remainingSize / options.Concurrency;
      chunkSize = (std::max(chunkSize, int64_t(1)) + c_grainSize - 1) / c_grainSize * c_grainSize;
      chunkSize = std::min(chunkSize, Details::c_FileDownloadDefaultChunkSize);
    }

    Storage::Details::ConcurrentTransfer(
        remainingOffset, remainingSize, chunkSize, options.Concurrency, downloadChunkFunc);
    ret->ContentLength = fileRangeSize;
    return ret;
  }

  Azure::Core::Response<DownloadFileToResult> FileClient::DownloadTo(
      const std::string& file,
      const DownloadFileToOptions& options) const
  {
    // Just start downloading using an initial chunk. If it's a small file, we'll get the whole
    // thing in one shot. If it's a large file, we'll get its full size in Content-Range and can
    // keep downloading it in chunks.
    int64_t firstChunkOffset = options.Offset.HasValue() ? options.Offset.GetValue() : 0;
    int64_t firstChunkLength = Details::c_FileDownloadDefaultChunkSize;
    if (options.InitialChunkSize.HasValue())
    {
      firstChunkLength = options.InitialChunkSize.GetValue();
    }
    if (options.Length.HasValue())
    {
      firstChunkLength = std::min(firstChunkLength, options.Length.GetValue());
    }

    DownloadFileOptions firstChunkOptions;
    firstChunkOptions.Context = options.Context;
    firstChunkOptions.Offset = options.Offset;
    if (firstChunkOptions.Offset.HasValue())
    {
      firstChunkOptions.Length = firstChunkLength;
    }

    Storage::Details::FileWriter fileWriter(file);

    auto firstChunk = Download(firstChunkOptions);

    int64_t fileSize;
    int64_t fileRangeSize;
    if (firstChunkOptions.Offset.HasValue())
    {
      fileSize = std::stoll(firstChunk->ContentRange.GetValue().substr(
          firstChunk->ContentRange.GetValue().find('/') + 1));
      fileRangeSize = fileSize - firstChunkOffset;
      if (options.Length.HasValue())
      {
        fileRangeSize = std::min(fileRangeSize, options.Length.GetValue());
      }
    }
    else
    {
      fileSize = firstChunk->BodyStream->Length();
      fileRangeSize = fileSize;
    }
    firstChunkLength = std::min(firstChunkLength, fileRangeSize);

    auto bodyStreamToFile = [](Azure::Core::Http::BodyStream& stream,
                               Storage::Details::FileWriter& fileWriter,
                               int64_t offset,
                               int64_t length,
                               Azure::Core::Context& context) {
      constexpr std::size_t bufferSize = 4 * 1024 * 1024;
      std::vector<uint8_t> buffer(bufferSize);
      while (length > 0)
      {
        int64_t readSize = std::min(static_cast<int64_t>(bufferSize), length);
        int64_t bytesRead
            = Azure::Core::Http::BodyStream::ReadToCount(context, stream, buffer.data(), readSize);
        if (bytesRead != readSize)
        {
          throw std::runtime_error("error when reading body stream");
        }
        fileWriter.Write(buffer.data(), bytesRead, offset);
        length -= bytesRead;
        offset += bytesRead;
      }
    };

    bodyStreamToFile(
        *(firstChunk->BodyStream), fileWriter, 0, firstChunkLength, firstChunkOptions.Context);
    firstChunk->BodyStream.reset();

    auto returnTypeConverter = [](Azure::Core::Response<DownloadFileResult>& response) {
      DownloadFileToResult ret;
      ret.ETag = std::move(response->ETag);
      ret.LastModified = std::move(response->LastModified);
      ret.HttpHeaders = std::move(response->HttpHeaders);
      ret.Metadata = std::move(response->Metadata);
      ret.IsServerEncrypted = response->IsServerEncrypted;
      return Azure::Core::Response<DownloadFileToResult>(
          std::move(ret),
          std::make_unique<Azure::Core::Http::RawResponse>(std::move(response.GetRawResponse())));
    };
    auto ret = returnTypeConverter(firstChunk);

    // Keep downloading the remaining in parallel
    auto downloadChunkFunc
        = [&](int64_t offset, int64_t length, int64_t chunkId, int64_t numChunks) {
            DownloadFileOptions chunkOptions;
            chunkOptions.Context = options.Context;
            chunkOptions.Offset = offset;
            chunkOptions.Length = length;
            auto chunk = Download(chunkOptions);
            bodyStreamToFile(
                *(chunk->BodyStream),
                fileWriter,
                offset - firstChunkOffset,
                chunkOptions.Length.GetValue(),
                chunkOptions.Context);

            if (chunkId == numChunks - 1)
            {
              ret = returnTypeConverter(chunk);
            }
          };

    int64_t remainingOffset = firstChunkOffset + firstChunkLength;
    int64_t remainingSize = fileRangeSize - firstChunkLength;
    int64_t chunkSize;
    if (options.ChunkSize.HasValue())
    {
      chunkSize = options.ChunkSize.GetValue();
    }
    else
    {
      int64_t c_grainSize = 4 * 1024;
      chunkSize = remainingSize / options.Concurrency;
      chunkSize = (std::max(chunkSize, int64_t(1)) + c_grainSize - 1) / c_grainSize * c_grainSize;
      chunkSize = std::min(chunkSize, Details::c_FileDownloadDefaultChunkSize);
    }

    Storage::Details::ConcurrentTransfer(
        remainingOffset, remainingSize, chunkSize, options.Concurrency, downloadChunkFunc);
    ret->ContentLength = fileRangeSize;
    return ret;
  }

  Azure::Core::Response<UploadFileFromResult> FileClient::UploadFrom(
      const uint8_t* buffer,
      std::size_t bufferSize,
      const UploadFileFromOptions& options) const
  {
    ShareRestClient::File::CreateOptions protocolLayerOptions;
    protocolLayerOptions.XMsContentLength = bufferSize;
    protocolLayerOptions.FileAttributes = FileAttributesToString(options.SmbProperties.Attributes);
    if (protocolLayerOptions.FileAttributes.empty())
    {
      protocolLayerOptions.FileAttributes = FileAttributesToString(FileAttributes::None);
    }
    if (options.SmbProperties.FileCreationTime.HasValue())
    {
      protocolLayerOptions.FileCreationTime = options.SmbProperties.FileCreationTime.GetValue();
    }
    else
    {
      protocolLayerOptions.FileCreationTime = std::string(c_FileDefaultTimeValue);
    }
    if (options.SmbProperties.FileLastWriteTime.HasValue())
    {
      protocolLayerOptions.FileLastWriteTime = options.SmbProperties.FileLastWriteTime.GetValue();
    }
    else
    {
      protocolLayerOptions.FileLastWriteTime = std::string(c_FileDefaultTimeValue);
    }
    if (options.FilePermission.HasValue())
    {
      protocolLayerOptions.FilePermission = options.FilePermission;
    }
    else if (options.SmbProperties.FilePermissionKey.HasValue())
    {
      protocolLayerOptions.FilePermissionKey = options.SmbProperties.FilePermissionKey;
    }
    else
    {
      protocolLayerOptions.FilePermission = std::string(c_FileInheritPermission);
    }

    if (!options.HttpHeaders.ContentType.empty())
    {
      protocolLayerOptions.FileContentType = options.HttpHeaders.ContentType;
    }
    if (!options.HttpHeaders.ContentEncoding.empty())
    {
      protocolLayerOptions.FileContentEncoding = options.HttpHeaders.ContentEncoding;
    }
    if (!options.HttpHeaders.ContentLanguage.empty())
    {
      protocolLayerOptions.FileContentLanguage = options.HttpHeaders.ContentLanguage;
    }
    if (!options.HttpHeaders.CacheControl.empty())
    {
      protocolLayerOptions.FileCacheControl = options.HttpHeaders.CacheControl;
    }
    if (!options.HttpHeaders.ContentDisposition.empty())
    {
      protocolLayerOptions.FileContentDisposition = options.HttpHeaders.ContentDisposition;
    }
    protocolLayerOptions.Metadata = options.Metadata;
    auto createResult = ShareRestClient::File::Create(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);

    int64_t chunkSize = options.ChunkSize.HasValue() ? options.ChunkSize.GetValue()
                                                     : Details::c_FileUploadDefaultChunkSize;

    auto uploadPageFunc = [&](int64_t offset, int64_t length, int64_t chunkId, int64_t numChunks) {
      unused(chunkId, numChunks);
      Azure::Core::Http::MemoryBodyStream contentStream(buffer + offset, length);
      UploadFileRangeOptions uploadRangeOptions;
      uploadRangeOptions.Context = options.Context;
      UploadRange(&contentStream, offset, uploadRangeOptions);
    };

    Storage::Details::ConcurrentTransfer(
        0, bufferSize, chunkSize, options.Concurrency, uploadPageFunc);

    UploadFileFromResult result;
    result.IsServerEncrypted = createResult->IsServerEncrypted;
    return Azure::Core::Response<UploadFileFromResult>(
        std::move(result),
        std::make_unique<Azure::Core::Http::RawResponse>(std::move(createResult.GetRawResponse())));
  }

  Azure::Core::Response<UploadFileFromResult> FileClient::UploadFrom(
      const std::string& file,
      const UploadFileFromOptions& options) const
  {
    Storage::Details::FileReader fileReader(file);

    ShareRestClient::File::CreateOptions protocolLayerOptions;
    protocolLayerOptions.XMsContentLength = fileReader.GetFileSize();
    protocolLayerOptions.FileAttributes = FileAttributesToString(options.SmbProperties.Attributes);
    if (protocolLayerOptions.FileAttributes.empty())
    {
      protocolLayerOptions.FileAttributes = FileAttributesToString(FileAttributes::None);
    }
    if (options.SmbProperties.FileCreationTime.HasValue())
    {
      protocolLayerOptions.FileCreationTime = options.SmbProperties.FileCreationTime.GetValue();
    }
    else
    {
      protocolLayerOptions.FileCreationTime = std::string(c_FileDefaultTimeValue);
    }
    if (options.SmbProperties.FileLastWriteTime.HasValue())
    {
      protocolLayerOptions.FileLastWriteTime = options.SmbProperties.FileLastWriteTime.GetValue();
    }
    else
    {
      protocolLayerOptions.FileLastWriteTime = std::string(c_FileDefaultTimeValue);
    }
    if (options.FilePermission.HasValue())
    {
      protocolLayerOptions.FilePermission = options.FilePermission;
    }
    else if (options.SmbProperties.FilePermissionKey.HasValue())
    {
      protocolLayerOptions.FilePermissionKey = options.SmbProperties.FilePermissionKey;
    }
    else
    {
      protocolLayerOptions.FilePermission = std::string(c_FileInheritPermission);
    }

    if (!options.HttpHeaders.ContentType.empty())
    {
      protocolLayerOptions.FileContentType = options.HttpHeaders.ContentType;
    }
    if (!options.HttpHeaders.ContentEncoding.empty())
    {
      protocolLayerOptions.FileContentEncoding = options.HttpHeaders.ContentEncoding;
    }
    if (!options.HttpHeaders.ContentLanguage.empty())
    {
      protocolLayerOptions.FileContentLanguage = options.HttpHeaders.ContentLanguage;
    }
    if (!options.HttpHeaders.CacheControl.empty())
    {
      protocolLayerOptions.FileCacheControl = options.HttpHeaders.CacheControl;
    }
    if (!options.HttpHeaders.ContentDisposition.empty())
    {
      protocolLayerOptions.FileContentDisposition = options.HttpHeaders.ContentDisposition;
    }

    protocolLayerOptions.Metadata = options.Metadata;
    auto createResult = ShareRestClient::File::Create(
        m_shareFileUri.ToString(), *m_pipeline, options.Context, protocolLayerOptions);

    int64_t chunkSize = options.ChunkSize.HasValue() ? options.ChunkSize.GetValue()
                                                     : Details::c_FileUploadDefaultChunkSize;

    auto uploadPageFunc = [&](int64_t offset, int64_t length, int64_t chunkId, int64_t numChunks) {
      unused(chunkId, numChunks);
      Azure::Core::Http::FileBodyStream contentStream(fileReader.GetHandle(), offset, length);
      UploadFileRangeOptions uploadRangeOptions;
      uploadRangeOptions.Context = options.Context;
      UploadRange(&contentStream, offset, uploadRangeOptions);
    };

    Storage::Details::ConcurrentTransfer(
        0, fileReader.GetFileSize(), chunkSize, options.Concurrency, uploadPageFunc);

    UploadFileFromResult result;
    result.IsServerEncrypted = createResult->IsServerEncrypted;
    return Azure::Core::Response<UploadFileFromResult>(
        std::move(result),
        std::make_unique<Azure::Core::Http::RawResponse>(std::move(createResult.GetRawResponse())));
  }
}}}} // namespace Azure::Storage::Files::Shares